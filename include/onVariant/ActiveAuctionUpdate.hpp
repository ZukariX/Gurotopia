#pragma once

struct auction_entry;

namespace on
{
    extern void ActiveAuctionUpdate(ENetPeer *peer, const ::auction_entry *auction, int delay = 0);
}
