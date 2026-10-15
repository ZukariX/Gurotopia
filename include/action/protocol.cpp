#include "pch.hpp"
#include "https/server_data.hpp"
#include "proton/Variant.hpp"

#include "protocol.hpp"

namespace
{
    bool read_credentials(::peer &player, const std::string &header)
    {
        player.growid.clear();
        player.password.clear();
        try
        {
            std::vector<std::string> pipes = readch(header, '|');
            if (pipes.size() < 4ull) throw std::runtime_error("");

            if (pipes[2ull] == "ltoken")
            {
                const std::string &decoded = base64_decode(pipes[3ull]);

                if (std::size_t pos = decoded.find("growId="); pos != std::string::npos)
                {
                    pos += strlen("growId=");
                    player.growid = decoded.substr(pos, decoded.find('&', pos) - pos);
                }
                if (std::size_t pos = decoded.find("password="); pos != std::string::npos)
                {
                    pos += strlen("password=");
                    player.password = decoded.substr(pos, decoded.find('&', pos) - pos);
                }
            } // @note delete decoded
            if (player.growid.empty() || player.password.empty()) throw std::runtime_error("");
        }
        catch (...) {
            return false;
        }
        return true;
    }
}

void action::protocol(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (!read_credentials(*pPeer, header))
    {
        send_action(*event.peer, "logon_fail", "");
        return;
    }
    if (!pPeer->load(pPeer->growid, pPeer->password))
    {
        send_action(*event.peer, "logon_fail", "");
        return;
    }

    send_varlist(event.peer, {
        "OnSendToServer",
        (signed)gServer_data.port,
        0,
        pPeer->user_id,
        std::format("{}|0|0", gServer_data.server), // @todo UUIDToken
        1,
        pPeer->growid.c_str()
    }); // @note PACKET_DISCONNECT from client.
}
