#include "pch.hpp"

#include "database/mail.hpp"
#include "mailbox_claimrewardsmany.hpp"

#include <charconv>

void action::mailbox_claimrewardsmany(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->user_id <= 0) return;
    const std::vector<std::string> values = readch(::hPipe{ header }["listIDs"], ',');
    std::vector<u_int> ids{};
    for (const std::string &value : values)
    {
        u_int id{};
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || id == 0) return;
        ids.push_back(id);
    }
    if (!claim_mail_rewards(event, ids)) return;
    std::vector<std::string> claimed{};
    for (u_int id : ids) claimed.push_back(std::to_string(id));
    send_varlist(event.peer, { "OnRequestClaimRewardsMany", 1, join(claimed, ",") });
}
