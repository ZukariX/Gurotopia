#include "pch.hpp"
#include "tile_internal.hpp"
#include "tile_change.hpp"
#include "item_activate_object.hpp"
#include "commands/weather.hpp"
#include "onVariant/NameChanged.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/random.hpp"
#include "tools/time.hpp"

namespace tiles
{
    namespace
    {
        ::pos punch_direction(const change &action)
        {
            const ::pos origin = action.packet.pos.by_32(true);
            const float x = action.player.facing_left ? -1.0f : 1.0f;
            const float y = action.packet.punch.y < origin.y ? -1.0f : 1.0f;
            if (action.packet.punch.y == origin.y) return {x, 0.0f};
            if (action.packet.punch.x == origin.x) return {0.0f, y};
            return {x, y};
        }

        void multi_punch(change &action)
        {
            static bool punching{};
            if (punching || action.player.clothing[clothing::HAND] != 5480) return;
            punching = true;
            const ::pos direction = punch_direction(action);
            ::gamePacket packet = action.packet;
            for (int i = 0; i < 2; ++i)
            {
                packet.punch.x += direction.x;
                packet.punch.y += direction.y;
                tile_change(action.event, packet);
            }
            punching = false;
        }

        void diggers_spade(change &action)
        {
            if (action.player.clothing[clothing::HAND] != 2952) return;
            if (action.item.id != 2 && action.item.id != 14) return;
            action.tile.hits[action.tile.fg == 0] = 3;
            const int color = action.item.id == 2 ? RandomRange(2, 3) : RandomRange(14, 15);
            send_particle_effect(action.event, action.packet.punch.by_32(), {color, 0x61});
        }

        void roulette(change &action)
        {
            if (action.item.id != 758) return;
            const u_char result = RandomRange(0, 36);
            const char color = result == 0 ? '2' : RandomRange(0, 3) < 2 ? 'b' : '4';
            const std::string message = std::format("[{} spun the wheel and got `{}{}``!]", action.player.display_growid, color, result);
            peers(action.player.recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &) {
                send_varlist(action.event.peer, {"OnTalkBubble", action.player.netid, message}, -1, 2000);
                on::ConsoleMessage(action.event.peer, message, 2000);
            });
        }

        void check_lock(const change &action)
        {
            if (is_tile_lock(action.item.id)) return;
            if (action.world.owner == action.player.user_id) return;
            ::peer owner;
            owner.user_id = action.world.owner;
            throw std::runtime_error(std::format("`5[```w{}`` `$World Locked`` by `w{}```5]``", action.world.name,
                owner.mysql_select<std::string>("growid", "", true)));
        }

        void check_breakable(const change &action)
        {
            switch (action.item.type)
            {
                case type::STRONG: throw std::runtime_error("It's too strong to break.");
                case type::MAIN_DOOR: throw std::runtime_error("(stand over and punch to use)");
                case type::LOCK: check_lock(action); break;
                default: break;
            }
        }

        struct reward_chance { int maximum; short id; };

        short choose_reward(std::span<const reward_chance> rewards, short fallback)
        {
            const auto found = std::ranges::find_if(rewards, [](const reward_chance &reward) {
                return RandomRange(0, reward.maximum) == 0;
            });
            return found == rewards.end() ? fallback : found->id;
        }

        void drop_gems(change &action, int gems, std::span<const short> denominations)
        {
            for (short amount : denominations)
                for (; gems >= amount; gems -= amount)
                    add_drop(action.event, {112, amount}, action.packet.punch.by_32(), action.world);
        }

        void provider(change &action)
        {
            static constexpr std::array<std::pair<short, short>, 6> products{{
                {872, 874}, {866, 868}, {1632, 1634}, {3888, 3890}, {5116, 5114}, {2798, 822}
            }};
            const auto found = std::ranges::find(products, action.item.id, &std::pair<short, short>::first);
            if (found != products.end())
                add_drop(action.event, {found->second, static_cast<short>(RandomRange(1, 2))}, action.packet.punch.by_32(), action.world);
            else if (action.item.id == 1008)
            {
                static constexpr std::array<short, 5> denominations{100, 50, 10, 5, 1};
                drop_gems(action, RandomRange(1, 100), denominations);
            }
            else if (action.item.id == 928)
            {
                static constexpr std::array<reward_chance, 4> chemicals{{{16, 918}, {8, 920}, {6, 924}, {4, 916}}};
                add_drop(action.event, {choose_reward(chemicals, 914), 1}, action.packet.punch.by_32(), action.world);
            }
            send_tile_update(action.event, action.packet, action.tile, action.world);
            action.player.add_xp(action.event, 1);
        }

        void harvest(change &action)
        {
            const auto tree = std::ranges::find(action.world.trees, action.packet.punch, &::tree::pos);
            if (tree == action.world.trees.end()) return;
            if (ticks() - tree->tick < action.item.tick) return;
            action.tile.hits[0] = 99;
            add_drop(action.event, {static_cast<short>(action.item.id - 1), static_cast<short>(RandomRange(1, tree->fruit * 3))},
                action.packet.punch.by_32(), action.world);
        }

        void weather(change &action)
        {
            action.tile.state[2] ^= S_TOGGLE;
            action.world.weather = action.packet.punch;
            peers(action.player.recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &connection) {
                send_varlist(&connection, {"OnSetCurrentWeather", action.tile.state[2] & S_TOGGLE ? get_weather_id(action.item.id) : 0});
            });
        }

        u_char random_value(change &action)
        {
            const u_char value = action.item.id == 456 ? RandomRange(0, 5) : action.item.id == 1300 ? RandomRange(1, 3) : 0;
            const auto found = std::ranges::find(action.world.random_blocks, action.packet.punch, &::random_block::pos);
            if (found == action.world.random_blocks.end()) action.world.random_blocks.emplace_back(value, action.packet.punch);
            else found->value = value;
            return value;
        }

        u_char punch_effect(change &action)
        {
            switch (action.item.type)
            {
                case type::WEATHER_MACHINE: weather(action); break;
                case type::TOGGLEABLE_BLOCK: case type::TOGGLEABLE_ANIMATED_BLOCK: case type::CHEST:
                    action.tile.state[2] ^= S_TOGGLE;
                    break;
                case type::RANDOM: return random_value(action);
                default: break;
            }
            return 0;
        }

        bool break_tile(change &action, u_char damage)
        {
            tile_apply_damage(action.event, action.packet, action.tile, damage);
            if (action.tile.hits[0] >= action.item.hits) { action.tile.fg = 0; action.tile.hits[0] = 0; }
            else if (action.tile.hits[1] >= action.item.hits) { action.tile.bg = 0; action.tile.hits[1] = 0; }
            else return false;
            action.tile.state[2] = 0;
            action.tile.state[3] &= ~S_VANISH;
            return true;
        }

        bool gift(const change &action)
        {
            static constexpr std::array<short, 3> ids{392, 3402, 9350};
            return std::ranges::find(ids, action.item.id) != ids.end();
        }

        void gift_reward(change &action)
        {
            static constexpr std::array<reward_chance, 12> rewards{{
                {99, 1458}, {20, 362}, {8, 366}, {8, 1470}, {20, 2384}, {4, 2396},
                {10, 3388}, {10, 2390}, {10, 3396}, {2, 3404}, {4, 3406}, {2, 3408}
            }};
            const short reward = choose_reward(rewards, 388);
            add_drop(action.event, {reward, static_cast<short>(reward == 3408 || reward == 3404 ? 10 : 1)},
                action.packet.punch.by_32(), action.world);
            if (reward == 1458)
            {
                const std::string message = std::format("msg|`4The Power of Love! `2{} found a `#Golden Heart Crystal`2 in a `#{}`2!",
                    action.player.growid, action.item.raw_name);
                peers(action.player.recent_worlds.back(), PEER_ALL, [&](ENetPeer &connection) { send_action(connection, "log", message); });
            }
            if (++action.player.gbc_pity % 100 == 0) modify_item_inventory(action.event, {9350, 1});
        }

        void remove_lock(change &action)
        {
            if (action.item.type != type::LOCK || is_tile_lock(action.item.id)) return;
            action.player.display_growid = std::format("`w{}``", action.player.growid);
            on::NameChanged(action.event);
            action.world.owner = 0;
        }

        int gem_limit(int rarity)
        {
            static constexpr std::array<std::pair<int, int>, 7> limits{{{87, 22}, {68, 18}, {53, 14}, {41, 11}, {36, 10}, {32, 9}, {24, 5}}};
            const auto found = std::ranges::find_if(limits, [rarity](const auto &limit) { return rarity >= limit.first; });
            return found == limits.end() ? 1 : found->second;
        }

        void ordinary_drops(change &action)
        {
            const int limit = gem_limit(action.item.rarity);
            const bool farmable = limit > 1;
            if (RandomRange(0, farmable ? 1 : 4) == 0)
            {
                static constexpr std::array<short, 3> denominations{10, 5, 1};
                drop_gems(action, RandomRange(1, limit), denominations);
            }
            if (RandomRange(0, farmable ? 2 : 4) == 0)
                add_drop(action.event, {static_cast<short>(action.item.id + 1), 1}, action.packet.punch.by_32(), action.world);
            else if (RandomRange(0, farmable ? 4 : 8) == 0)
                add_drop(action.event, {static_cast<short>(action.item.id), 1}, action.packet.punch.by_32(), action.world);
        }

        void break_reward(change &action)
        {
            if (gift(action)) gift_reward(action);
            else remove_lock(action);
            if (action.item.cat == CAT_RETURN)
            {
                const int uid = add_object(action.event, {static_cast<short>(action.item.id), 1}, action.packet.pos, action.world);
                item_activate_object(action.event, ::gamePacket{.id = uid, .punch = action.packet.punch});
                return;
            }
            if (u_char(action.item.property) & 4) return;
            if (action.item.type != type::SEED) ordinary_drops(action);
            action.player.add_xp(action.event, std::trunc(1.0f + action.item.rarity / 5.0f));
        }
    }

    bool punch(change &action)
    {
        multi_punch(action);
        diggers_spade(action);
        roulette(action);
        check_breakable(action);
        if (action.item.type == type::PROVIDER) { provider(action); return false; }
        if (action.item.type == type::SEED) harvest(action);
        if (!break_tile(action, punch_effect(action))) return false;
        break_reward(action);
        return true;
    }
}
