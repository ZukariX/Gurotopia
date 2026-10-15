#include "pch.hpp"
#include "tile_internal.hpp"
#include "tools/create_dialog.hpp"

namespace tiles
{
    namespace
    {
        void lock(change &action)
        {
            if (is_tile_lock(action.item.id)) return;
            if (action.player.user_id != action.world.owner) return;
            send_varlist(action.event.peer, {
                "OnDialogRequest",
                std::format(
                    "set_default_color|`o\n"
                    "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                    "add_popup_name|LockEdit|\n"
                    "add_label|small|`wAccess list:``|left\n"
                    "embed_data|tilex|{}\n"
                    "embed_data|tiley|{}\n"
                    "add_spacer|small|\n"
                    "add_label|small|Currently, you're the only one with access.``|left\n"
                    "add_spacer|small|\n"
                    "add_player_picker|playerNetID|`wAdd``|\n"
                    "add_checkbox|checkbox_public|Allow anyone to Build and Break|{}\n"
                    "add_checkbox|checkbox_disable_music|Disable Custom Music Blocks|{}\n"
                    "add_text_input|tempo|Music BPM|100|3|\n"
                    "add_checkbox|checkbox_disable_music_render|Make Custom Music Blocks invisible|0\n"
                    //"add_smalltext|Your current home world is: JOLEIT|left|\n"           // @todo only show when peer has a set home world.
                    "add_checkbox|checkbox_set_as_home_world|Set as Home World|0|\n"
                    "add_text_input|minimum_entry_level|World Level: |{}|3|\n"
                    "add_smalltext|Set minimum world entry level.|\n"
                    "add_button|sessionlength_dialog|`wSet World Timer``|noflags|0|0|\n"
                    "add_button|changecat|`wCategory: None``|noflags|0|0|\n"
                    "add_button|getKey|Get World Key|noflags|0|0|\n"
                    "end_dialog|lock_edit|Cancel|OK|\n",
                    action.item.raw_name, action.item.id, action.packet.punch.x, action.packet.punch.y, to_char(action.world.is_public), (action.world.lock_state & DISABLE_MUSIC) ? "1" : "0", action.world.minimum_entry_level
                )
            });
        }

        void door(change &action)
        {
            std::string label, dest, id;
            const auto found = std::ranges::find(action.world.doors, action.packet.punch, &::door::pos);
            if (found != action.world.doors.end()) { label = found->label; dest = found->dest; id = found->id; }
            send_varlist(action.event.peer, {
                "OnDialogRequest",
                std::format(
                    "set_default_color|`o\n"
                    "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                    "add_text_input|door_name|Label|{}|100|\n"
                    "add_popup_name|DoorEdit|\n"
                    "add_text_input|door_target|Destination|{}|24|\n"
                    "add_smalltext|Enter a Destination in this format: `2WORLDNAME:ID``|left|\n"
                    "add_smalltext|Leave `2WORLDNAME`` blank (:ID) to go to the door with `2ID`` in the `2Current World``.|left|\n"
                    "add_text_input|door_id|ID|{}|11|\n"
                    "add_smalltext|Set a unique `2ID`` to target this door as a Destination from another!|left|\n"
                    "add_checkbox|checkbox_locked|Is open to public|1\n"
                    "embed_data|tilex|{}\n"
                    "embed_data|tiley|{}\n"
                    "end_dialog|door_edit|Cancel|OK|", 
                    action.item.raw_name, action.item.id, label, dest, id, action.packet.punch.x, action.packet.punch.y
                )
            });
        }

        void sign(change &action)
        {
            std::string label;
            const auto found = std::ranges::find(action.world.signs, action.packet.punch, &::sign::pos);
            if (found != action.world.signs.end()) label = found->label;
            send_varlist(action.event.peer, {
                "OnDialogRequest",
                std::format(
                    "set_default_color|`o\n"
                    "add_popup_name|SignEdit|\n"
                    "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                    "add_textbox|What would you like to write on this sign?``|left|\n"
                    "add_text_input|sign_text||{}|128|\n"
                    "embed_data|tilex|{}\n"
                    "embed_data|tiley|{}\n"
                    "end_dialog|sign_edit|Cancel|OK|", 
                    action.item.raw_name, action.item.id, label, action.packet.punch.x, action.packet.punch.y
                )
            });
        }

        void entrance(change &action)
        {
            send_varlist(action.event.peer, {
                "OnDialogRequest",
                std::format(
                    "set_default_color|`o\n"
                    "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                    "add_checkbox|checkbox_public|Is open to public|{}\n"
                    "embed_data|tilex|{}\n"
                    "embed_data|tiley|{}\n"
                    "end_dialog|gateway_edit|Cancel|OK|\n", 
                    action.item.raw_name, action.item.id, to_char((action.tile.state[2] & S_PUBLIC)), action.packet.punch.x, action.packet.punch.y
                )
            });
        }

        void vending(change &action)
        {
            send_varlist(action.event.peer, {
                "OnDialogRequest",
                ::create_dialog()
                    .set_default_color("`o")
                    .add_label_with_icon("big", std::format("`w{}``", action.item.raw_name), action.item.id)
                    .add_spacer("small")
                    .embed_data("tilex", action.packet.punch.x)
                    .embed_data("tiley", action.packet.punch.y)
                    .add_textbox("This machine is empty.")
                    .add_item_picker("stockitem", "`wPut an item in``", "Choose an item to put in the machine!")
                    .add_smalltext("Upgrade to a DigiVend Machine for `44,000 Gems``.")
                    .add_button("upgradedigital", "Upgrade to DigiVend")
                    .add_spacer("small")
                    .end_dialog("vending", "Close", "")
            });
        }
    }

    void wrench(change &action)
    {
        switch (action.item.type)
        {
            case type::LOCK: lock(action); break;
            case type::DOOR: case type::PORTAL: door(action); break;
            case type::SIGN: sign(action); break;
            case type::ENTRANCE: entrance(action); break;
            case type::VENDING_MACHINE: vending(action); break;
            default: break;
        }
    }
}
