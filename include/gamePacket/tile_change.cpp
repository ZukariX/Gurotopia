#include "pch.hpp"
#include "tile_change.hpp"
#include "tile_internal.hpp"
#include "item_activate.hpp"

namespace
{
    bool restricted(const ::world &world, const ::peer &player)
    {
        return world.owner != 0 && !world.is_public && player.role == 0;
    }

    bool permitted(const tiles::change &action)
    {
        if (action.item.cat & CAT_PUBLIC) return true;
        if (!restricted(action.world, action.player)) return true;
        return action.player.user_id == action.world.owner ||
            std::ranges::find(action.world.access, action.player.user_id) != action.world.access.end();
    }

    bool extinguish(tiles::change &action)
    {
        if (!(action.tile.state[3] & S_FIRE)) return false;
        if (action.player.clothing[clothing::HAND] != 3066) return false;
        remove_fire(action.event, action.packet, action.tile, action.world);
        return true;
    }

    const ::item &active_item(const ::gamePacket &packet, const ::block &tile)
    {
        if (packet.id != 18 && packet.id != 32) return id_to_item(packet.id);
        return id_to_item(tile.fg != 0 ? tile.fg : tile.bg);
    }

    void dispatch(tiles::change &action)
    {
        if (action.packet.id == 18)
        {
            if (tiles::punch(action))
            {
                action.packet.netid = action.player.netid;
                state_visuals(*action.event.peer, ::gamePacket{action.packet});
            }
            return;
        }
        if (action.item.cloth_type != clothing::NONE)
        {
            if (action.packet.punch != action.player.pos.by_32(true))
                throw std::runtime_error("To wear clothing, use on yourself");
            item_activate(action.event, action.packet);
            return;
        }
        if (action.item.type == type::CONSUMEABLE) { tiles::consume(action); return; }
        if (action.packet.id == 32) { tiles::wrench(action); return; }
        tiles::place(action);
    }

    void apply(ENetEvent &event, const ::gamePacket &packet, ::peer &player)
    {
        auto world = std::ranges::find(worlds, player.recent_worlds.back(), &::world::name);
        if (world == worlds.end()) return;
        ::block *tile = world->tile(packet.punch);
        if (tile == nullptr) return;
        const ::item &item = active_item(packet, *tile);
        if (item.id == 0) return;
        tiles::change action{event, packet, player, *world, *tile, item};
        if (extinguish(action)) return;
        if (!permitted(action)) return;
        dispatch(action);
    }
}

void tile_change(ENetEvent &event, ::gamePacket packet)
{
    if (event.peer == nullptr || event.peer->data == nullptr) return;
    ::peer &player = *static_cast<::peer*>(event.peer->data);
    try { apply(event, packet, player); }
    catch (const std::exception &error)
    {
        send_varlist(event.peer, {"OnTalkBubble", player.netid, error.what(), 0u, 1u});
    }
}
