#include "pch.hpp"
#include "tile_internal.hpp"
#include "onVariant/NameChanged.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/random.hpp"
#include "tools/time.hpp"

namespace tiles
{
    namespace
    {
        bool recipe_matches(const ::item &recipe, int first, int second)
        {
            return (recipe.splice[0] == first && recipe.splice[1] == second) ||
                (recipe.splice[1] == first && recipe.splice[0] == second);
        }

        bool splice(change &action)
        {
            if (action.tile.state[2] == S_SPLICED) throw std::runtime_error("It would be too dangerous to try to mix three seeds.");
            const auto tree = std::ranges::find(action.world.trees, action.packet.punch, &::tree::pos);
            if (tree == action.world.trees.end()) return false;
            const auto recipe = std::ranges::find_if(items, [&](const ::item &item) {
                return recipe_matches(item, action.packet.id, action.tile.fg);
            });
            if (recipe == items.end())
                throw std::runtime_error(std::format("Hmm, it looks like `w{}`` and `w{}`` can't be spliced.",
                    id_to_item(action.tile.fg).raw_name, id_to_item(action.packet.id).raw_name));
            send_varlist(action.event.peer, {
                "OnTalkBubble", action.player.netid,
                std::format("`w{}`` and `w{}`` have been spliced to make a `${} Tree``!",
                    id_to_item(recipe->splice[0]).raw_name, id_to_item(recipe->splice[1]).raw_name,
                    recipe->raw_name.substr(0, recipe->raw_name.length() - 5)), 0u, 1u
            });
            tree->tick = ticks();
            action.tile.state[2] = S_SPLICED;
            action.tile.fg = recipe->id;
            return true;
        }

        bool place_on_tile(change &action)
        {
            if (action.tile.fg == 0) return false;
            switch (id_to_item(action.tile.fg).type)
            {
                case type::DISPLAY_BLOCK:
                    action.world.displays.emplace_back(action.item.id, action.packet.punch);
                    return true;
                case type::SEED: return splice(action);
                default: return false;
            }
        }

        bool occupied(const change &action)
        {
            if (action.item.type == type::BACKGROUND) return action.tile.bg != 0;
            return action.tile.fg != 0;
        }

        bool obstructs_player(const change &action)
        {
            return action.item.collision == collision::FULL && action.packet.punch == action.packet.pos.by_32(true);
        }

        void place_lock(change &action)
        {
            if (action.world.owner != 0)
                throw std::runtime_error("Only one `$World Lock`` can be placed in a world, you'd have to remove the other one first.");
            action.world.owner = action.player.user_id;
            action.player.display_growid = std::format("`2{}``", action.player.growid);
            on::NameChanged(action.event);
            if (std::ranges::find(action.player.my_worlds, action.world.name) == action.player.my_worlds.end())
            {
                std::ranges::rotate(action.player.my_worlds, action.player.my_worlds.begin() + 1);
                action.player.my_worlds.back() = action.world.name;
            }
            const std::string message = std::format("`5[```w{}`` has been `$World Locked`` by {}`5]``", action.world.name, action.player.growid);
            peers(action.player.recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &connection) {
                send_varlist(&connection, {"OnTalkBubble", action.player.netid, message});
                on::ConsoleMessage(&connection, message);
            });
        }

        bool tile_data(change &action)
        {
            switch (action.item.type)
            {
                case type::DOOR: action.world.doors.emplace_back("", "", "", action.packet.punch); return true;
                case type::SIGN: action.world.signs.emplace_back("", action.packet.punch); return true;
                case type::SEED:
                    action.world.trees.emplace_back(ticks(), RandomRange(1, 3), action.packet.punch);
                    action.tile.state[2] = 0x11;
                    return true;
                case type::ENTRANCE: action.tile.state[2] |= S_PUBLIC; return true;
                default: return false;
            }
        }

        void placement_visuals(change &action, bool tile_update, bool lock_visuals)
        {
            action.packet.netid = action.player.netid;
            state_visuals(*action.event.peer, ::gamePacket{action.packet});
            if (tile_update) send_tile_update(action.event, action.packet, action.tile, action.world);
            if (lock_visuals) state_visuals(*action.event.peer, ::gamePacket{
                .type = 0x0f, .netid = action.world.owner, .state = state::S_EXTENDED,
                .id = action.packet.id, .punch = action.packet.punch
            });
        }
    }

    void place(change &action)
    {
        if (place_on_tile(action))
        {
            modify_item_inventory(action.event, {static_cast<short>(action.packet.id), -1});
            send_tile_update(action.event, action.packet, action.tile, action.world);
            return;
        }
        if (occupied(action) || obstructs_player(action)) return;
        const bool lock_visuals = action.item.type == type::LOCK && !is_tile_lock(action.item.id);
        if (lock_visuals) place_lock(action);
        const bool tile_update = tile_data(action);
        action.tile.state[2] |= action.player.facing_left ? S_LEFT : S_RIGHT;
        if (action.item.type == type::BACKGROUND) action.tile.bg = action.packet.id;
        else action.tile.fg = action.packet.id;
        action.player.emplace({static_cast<short>(action.item.id), -1});
        placement_visuals(action, tile_update, lock_visuals);
    }
}
