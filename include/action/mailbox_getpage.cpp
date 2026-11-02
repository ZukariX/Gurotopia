#include "pch.hpp"

#include "database/mail.hpp"
#include "mailbox_getpage.hpp"

#include <algorithm>
#include <charconv>

void action::mailbox_getpage(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->user_id <= 0) return;
    const std::string input = ::hPipe{ header }["pageIndex"];
    int page{};
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), page);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || page < -1) return;
    std::vector<::mail> mails{};
    if (!load_mail(pPeer->user_id, mails)) return;
    const int total = static_cast<int>(mails.size());
    const int unread = std::ranges::count(mails, 0, &::mail::is_read);
    if (page == -1) send_mailbox_button(event.peer);
    else mails.clear();
    send_varlist(event.peer, { "OnRequestMailPage", 1, mail_data(mails), total, unread });
}
