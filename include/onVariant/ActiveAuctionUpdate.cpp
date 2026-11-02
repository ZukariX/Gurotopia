#include "pch.hpp"

#include "database/auction.hpp"
#include "ActiveAuctionUpdate.hpp"

void on::ActiveAuctionUpdate(ENetPeer *peer, const ::auction_entry *auction, int delay)
{
    if (!auction)
    {
        send_varlist(peer, { "OnActiveAuctionUpdate", "{\"Active\":false}" }, -1, delay);
        return;
    }
    const ::peer *pPeer = static_cast<::peer*>(peer->data);
    int placed{};
    for (const auto &[uid, amount] : auction->bids)
        if (uid == pPeer->user_id) placed = amount;
    send_varlist(peer, { "OnActiveAuctionUpdate", std::format(
        "{{\"Active\":true,\"AlreadyPlaced\":{},\"AvailableLocks\":{},\"BidEndTime\":{},\"HighestBidUserName\":\"{}\",\"HighestBidWlCount\":{},\"ItemId\":{}}}",
        placed, pPeer->wl_storage, auction->end_time, auction->bidder == pPeer->user_id ? "You are the highest bidder" : "",
        auction->bid, auction->item) }, -1, delay);
}
