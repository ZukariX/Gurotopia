#include "pch.hpp"
#include "action/join_request.hpp"
#include "action/quit_to_exit.hpp"
#include "tile_activate.hpp"

namespace
{
    void use_door(ENetEvent &event, const ::gamePacket &packet, ::world &world)
    {
        const ::peer &player = *static_cast<::peer*>(event.peer->data);
        const auto door = std::ranges::find(world.doors, packet.punch, &::door::pos);
        if (door != world.doors.end() && !door->dest.empty())
        {
            const std::string destination = door->dest;
            action::quit_to_exit(event, "", true);
            action::join_request(event, "", destination);
            return;
        }
        send_varlist(event.peer, { "OnSetPos", CL_Vec2f{player.rest_pos.x, player.rest_pos.y} }, player.netid);
        send_varlist(event.peer, { "OnZoomCamera", 10000.0f, 1000u });
        send_varlist(event.peer, { "OnSetFreezeState", 0u }, player.netid);
    }
}

void tile_activate(ENetEvent& event, ::gamePacket gamePacket)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    ::block *tile = world->tile(gamePacket.punch);
    if (tile == nullptr) return;
    ::block &block = *tile;
    const ::item &item = id_to_item(block.fg);
    switch (item.type)
    {
        case type::MAIN_DOOR:
        {
            action::quit_to_exit(event, "", false);
            return;
        }
        case type::DOOR:
        case type::PORTAL:
            use_door(event, gamePacket, *world);
            return;
        case type::CHECKPOINT:
        {
            if (::block *checkpoint = world->tile(pPeer->rest_pos.by_32(true)))
            {
                checkpoint->state[2] &= ~S_TOGGLE;
                send_tile_update(event, ::gamePacket{.id = block.fg, .punch = pPeer->rest_pos.by_32(true)}, *checkpoint, *world);
            }

            pPeer->rest_pos = gamePacket.punch.by_32();
            block.state[2] |= S_TOGGLE; // @note toggle current checkpoint
            send_tile_update(event, ::gamePacket{.id = block.fg, .punch = gamePacket.punch}, block, *world);
            break;
        }
    }

    state_visuals(*event.peer, std::move(gamePacket)); // finished.
}