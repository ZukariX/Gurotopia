#include "pch.hpp"
#include "action/__action.hpp"
#include "gamePacket/_gamePacket.hpp"
#include "tools/login.hpp"
#include "receive.hpp"
#include <memory>

namespace
{
    void text_packet(ENetEvent &event, const std::span<const enet_uint8> data)
    {
        if (data.size() < 5) return;
        std::string header{data.begin() + 4, data.end() - 1};
        std::ranges::replace(header, '\n', '|');
        const auto fields = readch(header, '|');
        if (fields.size() < 2) return;
        const std::string action = fields[0] == "protocol" || fields[0] == "tankIDName"
            ? fields[0] : std::format("{}|{}", fields[0], fields[1]);
        const ::peer &player = *static_cast<::peer*>(event.peer->data);
        if (!login::allowed(player, action)) return;
        if (const auto found = action_pool.find(action); found != action_pool.end()) found->second(event, header);
    }

    void game_packet(ENetEvent &event, const std::span<const enet_uint8> data)
    {
        const ::peer &player = *static_cast<::peer*>(event.peer->data);
        if (!player.authenticated || player.netid == 0) return;
        if (data.size() < sizeof(::gamePacket)) return;
        ::gamePacket packet = make_gamePacket(data.data());
        if (packet.size > data.size() - sizeof(::gamePacket)) return;
        if (const auto found = gamePacket_pool.find(packet.type); found != gamePacket_pool.end()) found->second(event, packet);
    }

    void dispatch_packet(ENetEvent &event, const std::span<const enet_uint8> data)
    {
        switch (data[0])
        {
            case 2: case 3: text_packet(event, data); break;
            case 4: game_packet(event, data); break;
        }
    }

}

void receive(ENetEvent &event)
{
    const std::unique_ptr<ENetPacket, decltype(&enet_packet_destroy)> packet{event.packet, enet_packet_destroy};
    if (!event.peer || !event.peer->data || !event.packet) return;
    if (event.packet->dataLength < 4) return;
    const std::span<const enet_uint8> data{event.packet->data, event.packet->dataLength};
    try
    {
        dispatch_packet(event, data);
    }
    catch (const std::exception &) { std::fputs("Dropped an invalid packet.\n", stderr); }
}
