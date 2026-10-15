#include "pch.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/punch.hpp"
#include "database/inventory.hpp"
#include "item_activate.hpp"

namespace
{
    void equip(ENetEvent &event, const ::item &item)
    {
        ::peer &player = *static_cast<::peer*>(event.peer->data);
        if (item.cloth_type >= player.clothing.size()) return;
        float &current = player.clothing[item.cloth_type];
        current = current == item.id ? 0 : item.id;
        player.update_effects();
        const u_char effect = get_punch_id(static_cast<u_int>(current));
        if (effect != 0) player.punch_effect = effect;
        send_varlist(event.peer, { "OnEquipNewItem", item.id }, player.netid);
        on::SetClothing(*event.peer);
    }

    struct conversion
    {
        ::slot take;
        ::slot give;
        const char *message;
    };

    void convert(ENetEvent &event, int id)
    {
        static const std::array<conversion, 4> conversions{{
            {{242, 100}, {1796, 1}, "You compressed 100 `2World Lock`` into a `2Diamond Lock``!"},
            {{1796, 1}, {242, 100}, "You shattered a `2Diamond Lock`` into 100 `2World Lock``!"},
            {{1486, 100}, {6802, 1}, ""},
            {{6802, 1}, {1486, 100}, ""}
        }};
        for (const auto &conversion : conversions)
        {
            if (conversion.take.id != id) continue;
            if (!exchange_items(event, {conversion.take}, {conversion.give})) return;
            if (*conversion.message == '\0') return;
            const ::peer &player = *static_cast<::peer*>(event.peer->data);
            send_varlist(event.peer, { "OnTalkBubble", player.netid, conversion.message, 0u, 1u });
            on::ConsoleMessage(event.peer, conversion.message);
            return;
        }
    }
}

void item_activate(ENetEvent &event, ::gamePacket packet)
{
    const ::peer &player = *static_cast<::peer*>(event.peer->data);
    if (!inventory::valid_id(packet.id) || player.held(packet.id) <= 0) return;
    const ::item &item = id_to_item(packet.id);
    if (item.cloth_type != clothing::NONE) equip(event, item);
    else convert(event, packet.id);
}
