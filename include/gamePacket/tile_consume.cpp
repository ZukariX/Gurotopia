#include "pch.hpp"
#include "tile_internal.hpp"
#include "action/quit_to_exit.hpp"
#include "action/join_request.hpp"
#include "onVariant/Action.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/create_dialog.hpp"

namespace tiles
{
    namespace
    {
        struct effect { float color{}; float particle{}; };

        void blast_dialog(change &action)
        {
            if (action.item.raw_name.find(" Blast") == std::string::npos) return;
            send_varlist(action.event.peer, {
                "OnDialogRequest", std::format(
                    "set_default_color|`o\n"
                    "embed_data|id|{0}\n"
                    "add_label_with_icon|big|`w{1}``|left|{0}|\n"
                    "add_label|small|This item creates a new world! Enter a unique name for it.|left\n"
                    "add_text_input|name|New World Name||24|\n"
                    "end_dialog|create_blast|Cancel|Create!|\n",
                    action.item.id, action.item.raw_name)
            });
        }

        void check_dye(change &action)
        {
            if (action.item.raw_name.find("Hair Dye") == std::string::npos) return;
            if (action.packet.punch != action.player.pos.by_32(true)) throw std::runtime_error("Don't spill your dye!");
            if (action.tile.fg != 230) throw std::runtime_error("You'll make a huge mess if you do that outside the Bathtub!");
            on::Action(action.event, "shower");
            send_varlist(action.event.peer, {"OnTalkBubble", action.player.netid, "You dyed your hair!", 0u, 1u});
        }

        void check_consumable(change &action)
        {
            blast_dialog(action);
            if (action.item.raw_name.find("Paint Bucket - ") != std::string::npos && action.player.clothing[clothing::HAND] != 3494)
                throw std::runtime_error("you need a Paintbrush to apply paint!");
            check_dye(action);
        }

        bool move_door(change &action, effect &)
        {
            if (action.world.tile({action.packet.punch.x, action.packet.punch.y + 1.0f}) == nullptr)
                throw std::runtime_error("There's no room to put the door there! You need 2 empty spaces vertically.");
            if (!door_mover(action.world, action.packet.punch))
                throw std::runtime_error("There's no room to put the door there! You need 2 empty spaces vertically.");
            const std::string name = action.world.name;
            peers(action.player.recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &connection) {
                ENetEvent event{.peer = &connection};
                action::quit_to_exit(event, "", true);
                action::join_request(event, "", name);
            });
            return false;
        }

        bool water(change &action, effect &)
        {
            if (action.tile.state[3] & S_FIRE) remove_fire(action.event, action.packet, action.tile, action.world);
            else action.tile.state[3] ^= S_WATER;
            return true;
        }

        bool glue(change &action, effect &)
        {
            action.tile.state[3] ^= S_GLUE;
            return true;
        }

        bool lighter(change &action, effect &visual)
        {
            if (action.tile.fg == 0 && action.tile.bg == 0) throw std::runtime_error("There's nothing to burn!");
            if (action.tile.state[3] & (S_FIRE | S_WATER)) return false;
            action.tile.state[3] |= S_FIRE;
            const std::string message = "`7[```4MWAHAHAHA!! FIRE FIRE FIRE```7]``";
            peers(action.player.recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &) {
                send_varlist(action.event.peer, {"OnTalkBubble", action.player.netid, message, 0u});
                on::ConsoleMessage(action.event.peer, message);
            });
            visual.particle = 0x96;
            if (action.tile.fg == 3090) action.tile.fg = 3128;
            return true;
        }

        bool lollipop(change &action, effect &)
        {
            send_varlist(action.event.peer, {"OnTalkBubble", action.player.netid, "`#YUM!:D", 0u});
            return true;
        }

        bool love_potion(change &action, effect &visual)
        {
            if (action.tile.fg != 10) return false;
            action.tile.fg = 392;
            visual.particle = 0x2c;
            return true;
        }

        bool experience(change &action, effect &)
        {
            send_varlist(action.event.peer, {"OnTalkBubble", action.player.netid, "`#GULP! You got smarter!", 0u});
            action.player.add_xp(action.event, 10000);
            return true;
        }

        bool firework(change &action, effect &)
        {
            fireworks(action.event, action.packet.punch.by_32());
            return true;
        }

        bool megaphone(change &action, effect &)
        {
            send_varlist(action.event.peer, {
                "OnDialogRequest", ::create_dialog()
                    .set_default_color("`o")
                    .add_label_with_icon("big", "`wMegaphone``", action.item.id)
                    .add_textbox("Enter a message you want to broadcast to every player in Growtopia! This will use up 1 Megaphone")
                    .add_text_input("message", "", "", 128)
                    .end_dialog("megaphone", "Nevermind", "Broadcast")
            });
            return true;
        }

        bool duct_tape(change &action, effect &)
        {
            peers(action.player.recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &connection) {
                ::peer &player = *static_cast<::peer*>(connection.data);
                if (action.packet.punch != player.pos.by_32(true)) return;
                player.state |= S_DUCT_TAPE;
                on::SetClothing(connection);
            });
            return true;
        }

        bool hair_dye(change &, effect &) { return true; }

        bool paint(change &action, effect &visual)
        {
            struct paint_color { short id; u_char mask; u_int color; };
            static constexpr std::array<paint_color, 8> colors{{
                {3478, S_RED, bgra::RED}, {3480, S_YELLOW, bgra::GREEN | bgra::RED},
                {3482, S_GREEN, bgra::GREEN}, {3484, S_AQUA, bgra::BLUE | bgra::GREEN},
                {3486, S_BLUE, bgra::BLUE}, {3488, S_PURPLE, bgra::BLUE | bgra::RED},
                {3490, S_CHARCOAL, bgra::BLUE | bgra::GREEN | bgra::RED | bgra::ALPHA},
                {3492, S_VANISH, bgra::BLUE | bgra::GREEN | bgra::RED}
            }};
            const auto found = std::ranges::find(colors, action.item.id, &paint_color::id);
            if (found == colors.end()) return false;
            if (found->id == 3492) action.tile.state[3] &= ~found->mask;
            else action.tile.state[3] |= found->mask;
            visual.color = found->color;
            visual.particle = 0xa8;
            return true;
        }

        bool use(change &action, effect &visual)
        {
            using handler = bool (*)(change &, effect &);
            static constexpr std::array<std::pair<short, handler>, 14> handlers{{
                {1404, move_door}, {822, water}, {1866, glue}, {3062, lighter},
                {3404, lollipop}, {3406, lollipop}, {3400, love_potion}, {1488, experience},
                {834, firework}, {2480, megaphone}, {408, duct_tape},
                {3822, hair_dye}, {3824, hair_dye}, {3826, hair_dye}
            }};
            const auto found = std::ranges::find(handlers, action.item.id, &std::pair<short, handler>::first);
            if (found == handlers.end()) return false;
            return found->second(action, visual);
        }
    }

    void consume(change &action)
    {
        check_consumable(action);
        effect visual;
        if (!paint(action, visual) && !use(action, visual)) return;
        if (visual.particle > 0.0f) send_particle_effect(action.event, action.packet.punch.by_32(), {visual.color, visual.particle});
        send_tile_update(action.event, action.packet, action.tile, action.world);
        modify_item_inventory(action.event, {static_cast<short>(action.item.id), -1});
        action.player.add_xp(action.event, 1);
    }
}
