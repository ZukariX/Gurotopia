#include "pch.hpp"

#include "database/mail.hpp"
#include "mailbox_claimreward.hpp"

#include <charconv>

void action::mailbox_claimreward(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->user_id <= 0) return;
    const std::string input = ::hPipe{ header }["messageID"];
    u_int id{};
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || id == 0) return;
    std::vector<u_int> ids{ id };
    if (claim_mail_rewards(event, ids)) send_varlist(event.peer, { "OnRequestClaimMailReward", 1 });
}
