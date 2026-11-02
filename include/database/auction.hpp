#pragma once

#include <utility>

struct auction_entry
{
    u_int id{};
    std::string world{};
    ::pos pos{};
    int owner{};
    u_short item{};
    u_int price{};
    u_int bid{};
    u_char duration{1};
    u_int end_time{};
    int bidder{};
    std::vector<std::pair<int, int>> bids{};

    bool mysql_update();
    void update_block(::block &block) const;
};

extern std::vector<::auction_entry> auctions;
extern void load_auctions();
extern void restore_auctions(::world &world);
extern void update_auctions();
extern ::auction_entry *viewed_auction(const ::peer &peer);
extern void send_auction_hud(ENetPeer *peer);
extern void refresh_auction_huds(const std::string &world);
extern bool modify_world_locks(int uid, int amount);
