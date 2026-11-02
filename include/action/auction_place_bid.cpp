#include "pch.hpp"

#include "database/auction.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "auction_place_bid.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <limits>

void action::auction_place_bid(ENetEvent &event, const std::string &header)
{
    const std::string input = ::hPipe{header}["bidAmount"];
    int amount{};
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), amount);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size()) return;
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::auction_entry *auction = viewed_auction(*pPeer);
    const u_int now = static_cast<u_int>(std::time(nullptr));
    if (!auction || pPeer->netid == 0 || pPeer->auction_world != auction->world || pPeer->auction_pos != auction->pos ||
        auction->owner == pPeer->user_id || auction->end_time <= now || (amount != 1 && amount != 5 && amount != 10) ||
        auction->bid > static_cast<u_int>(std::numeric_limits<int>::max() - amount)) return;
    int placed{};
    for (const auto &[uid, value] : auction->bids)
        if (uid == pPeer->user_id) placed = value;
    const int cost = static_cast<int>(auction->bid) + amount - placed;
    if (cost <= 0 || cost > pPeer->wl_storage) return;
    const ::auction_entry previous = *auction;
    auction->bid += amount;
    auction->bidder = pPeer->user_id;
    auto bid = std::ranges::find_if(auction->bids, [&](const auto &entry) { return entry.first == pPeer->user_id; });
    if (bid == auction->bids.end()) auction->bids.emplace_back(pPeer->user_id, static_cast<int>(auction->bid));
    else bid->second = static_cast<int>(auction->bid);
    if (auction->end_time - now < 30/*seconds*/) auction->end_time += 30;
    const bool saved = mysql_query(db.mysql, "START TRANSACTION") == 0 && modify_world_locks(pPeer->user_id, -cost) && auction->mysql_update();
    if (!saved || mysql_query(db.mysql, "COMMIT"))
    {
        mysql_query(db.mysql, "ROLLBACK");
        *auction = previous;
        return;
    }
    pPeer->wl_storage -= cost;
    auto world = std::ranges::find(worlds, auction->world, &::world::name);
    if (world != worlds.end()) auction->update_block(world->blocks[cord(auction->pos.x_int(), auction->pos.y_int())]);
    refresh_auction_huds(auction->world);
    const std::string bubble = std::format("CP:0_PL:2_OID:_Bid +{}!", amount);
    peers(auction->world, PeerSearch::WORLD, [&](ENetPeer *peer)
    {
        send_varlist(peer, { "OnTalkBubble", static_cast<u_int>(pPeer->netid), bubble, 0u });
        on::ConsoleMessage(peer, std::format("CP:0_PL:2_OID:_CT:[W]_ `6<{}>`` `$Bid +{}!``", pPeer->display_growid, amount), pPeer->netid);
    });
    send_varlist(event.peer, { "OnConsoleMessage", std::format("Auction: {} min remaining for `5{}``, current bid is `5{} wl``",
        (auction->end_time - now + 59) / 60, id_to_item(auction->item).raw_name, auction->bid) });
    peers("", PeerSearch::EVERYONE, [&](ENetPeer *peer)
    {
        ::peer *pOthers = static_cast<::peer*>(peer->data);
        if (!pOthers || pOthers->user_id <= 0) return;
        if (pOthers->recent_worlds.back() != auction->world && std::ranges::any_of(auction->bids,
            [&](const auto &entry) { return entry.first == pOthers->user_id; }))
            send_varlist(peer, { "OnAuctionBidAmountUpdate", static_cast<int>(auction->id), pPeer->user_id,
                static_cast<int>(auction->bid), auction->end_time });
        if (previous.bidder > 0 && previous.bidder != pPeer->user_id && previous.bidder == pOthers->user_id)
        {
            on::ConsoleMessage(peer, std::format("{} outbid you by {} in the auction in the world {}", pPeer->growid, amount, auction->world));
            send_varlist(peer, { "OnAuctionOutbid", auction->world, pPeer->user_id, amount });
        }
    });
}
