#include "pch.hpp"
#include "login.hpp"
#include <chrono>
#include <unordered_map>
#include <openssl/rand.h>

namespace login
{
    namespace
    {
        using clock = std::chrono::steady_clock;
        struct pending
        {
            int token, uid;
            std::string growid, password;
            clock::time_point expires;
        };
        std::unordered_map<std::string, pending> handoffs;

        bool same_name(const std::string &first, const std::string &second)
        {
            if (first.size() != second.size()) return false;
            auto next = second.begin();
            return std::ranges::all_of(first, [&next](unsigned char c) {
                return std::tolower(c) == std::tolower(static_cast<unsigned char>(*next++));
            });
        }

        bool connected(int uid)
        {
            if (host == nullptr) return false;
            for (const auto *connection : peers())
            {
                const auto *player = static_cast<const ::peer*>(connection->data);
                if (player == nullptr) continue;
                if (player->authenticated && player->user_id == uid) return true;
            }
            return false;
        }

        bool matches(const pending &saved, const ::hPipe &packet)
        {
            return packet["token"] == std::to_string(saved.token) && same_name(packet["tankIDName"], saved.growid);
        }

        void prune()
        {
            const auto now = clock::now();
            std::erase_if(handoffs, [now](const auto &entry) { return entry.second.expires <= now; });
        }
    }

    bool issue(const ::peer &player, ticket &result)
    {
        if (!player.loaded || player.user_id <= 0) return false;
        prune();
        if (handoffs.size() >= 512) return false;
        std::array<unsigned char, 20> random{};
        if (RAND_bytes(random.data(), random.size()) != 1) return false;
        u_int token{};
        for (std::size_t i = 0; i < 4; ++i) token = token << 8 | random[i];
        result.token = static_cast<int>(token & 0x7fffffff);
        if (result.token == 0) result.token = 1;
        result.uuid.clear();
        for (std::size_t i = 4; i < random.size(); ++i) result.uuid += std::format("{:02x}", random[i]);
        return handoffs.emplace(result.uuid, pending{result.token, player.user_id, player.growid, player.password,
                                clock::now() + std::chrono::seconds(60)}).second;
    }

    bool consume(::peer &player, const ::hPipe &packet)
    {
        prune();
        const auto found = handoffs.find(packet["UUIDToken"]);
        if (found == handoffs.end()) return false;
        const pending saved = found->second;
        if (!matches(saved, packet) || connected(saved.uid)) return false;
        handoffs.erase(found);
        if (!player.load_existing(saved.growid)) return false;
        if (player.user_id != saved.uid || player.password != saved.password) return false;
        player.authenticated = true;
        return true;
    }

    bool allowed(const ::peer &player, std::string_view action)
    {
        if (action == "action|quit") return true;
        if (action == "protocol" || action == "tankIDName") return !player.authenticated;
        return player.authenticated;
    }

    void refuse(ENetEvent &event)
    {
        send_action(*event.peer, "logon_fail", "");
        enet_peer_disconnect_later(event.peer, 0);
    }
}
