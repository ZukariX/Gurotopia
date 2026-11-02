#pragma once

struct mail
{
    u_int id{}, item{}, expires{};
    int uid{}, amount{}, is_read{}, claimed{};
    std::string kind{}, world{};
};

extern bool add_auction_mail(int uid, const std::string &kind, u_short item, const std::string &world, int amount);
extern bool load_mail(int uid, std::vector<::mail> &mails);
extern std::string mail_data(const std::vector<::mail> &mails);
extern void send_mailbox_button(ENetPeer *peer);
extern bool mark_mail_read(int uid, u_int id);
extern bool claim_mail_rewards(ENetEvent &event, std::vector<u_int> &ids);
