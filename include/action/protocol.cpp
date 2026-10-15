#include "pch.hpp"
#include "https/server_data.hpp"
#include "tools/login.hpp"
#include "protocol.hpp"

namespace
{
    std::string field(const std::string &body, const std::string &key)
    {
        for (const auto &part : readch(body, '&'))
        {
            if (!part.starts_with(key + "=")) continue;
            return part.substr(key.size() + 1);
        }
        return {};
    }
}

void action::protocol(ENetEvent &event, const std::string &header)
{
    ::peer &player = *static_cast<::peer*>(event.peer->data);
    if (player.authenticated) return;
    const std::string decoded = base64_decode(::hPipe{header}["ltoken"]);
    const std::string name = field(decoded, "growId");
    const std::string password = field(decoded, "password");
    if (!player.load(name, password)) { login::refuse(event); return; }
    login::ticket ticket;
    if (!login::issue(player, ticket)) { login::refuse(event); return; }
    send_varlist(event.peer, {
        "OnSendToServer", static_cast<int>(gServer_data.port), ticket.token, player.user_id,
        std::format("{}|0|{}", gServer_data.server, ticket.uuid), 1, player.growid
    });
}
