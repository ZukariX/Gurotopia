#include "pch.hpp"

#include "database/auction.hpp"
#include "onVariant/BillboardChange.hpp"
#include "auction_start_prompt.hpp"

#include <algorithm>
#include <ctime>

void auction_start_prompt(ENetEvent &event, const ::hPipe &)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto world = std::ranges::find(worlds, pPeer->auction_world, &::world::name);
    if (world == worlds.end() || pPeer->user_id <= 0 || pPeer->netid == 0 || world->name != pPeer->recent_worlds.back() ||
        world->owner != pPeer->user_id || pPeer->auction_item == 0 || pPeer->auction_price == 0 ||
        pPeer->auction_price > 9999999 || (pPeer->auction_duration != 1 && pPeer->auction_duration != 3 && pPeer->auction_duration != 12) ||
        !pPeer->trade_offers.empty()) return;
    const int tile = cord(pPeer->auction_pos.x_int(), pPeer->auction_pos.y_int());
    if (pPeer->auction_pos.x < 0 || pPeer->auction_pos.x >= 100 || pPeer->auction_pos.y < 0 ||
        tile >= static_cast<int>(world->blocks.size()) || world->blocks[tile].fg != 15546/*Auction Block*/) return;
    if (std::ranges::any_of(auctions, [&](const ::auction_entry &auction)
        { return auction.world == world->name && auction.pos == pPeer->auction_pos; })) return;
    auto slot = std::ranges::find(pPeer->slots, static_cast<short>(pPeer->auction_item), &::slot::id);
    if (slot == pPeer->slots.end() || slot->count <= 0) return;
    ::auction_entry auction{.world = world->name, .pos = pPeer->auction_pos, .owner = pPeer->user_id,
        .item = pPeer->auction_item, .price = pPeer->auction_price, .bid = pPeer->auction_price,
        .duration = pPeer->auction_duration, .end_time = static_cast<u_int>(std::time(nullptr)) + pPeer->auction_duration * 3600u};
    const ::block previous = world->blocks[tile];
    --slot->count;
    auctions.push_back(auction);
    auction.update_block(world->blocks[tile]);
    const bool saved = mysql_query(db.mysql, "START TRANSACTION") == 0 && pPeer->mysql_update_all() &&
        auctions.back().mysql_update() && world->mysql_update_all();
    if (!saved || mysql_query(db.mysql, "COMMIT"))
    {
        mysql_query(db.mysql, "ROLLBACK");
        auctions.pop_back();
        world->blocks[tile] = previous;
        ++slot->count;
        return;
    }
    ++slot->count;
    if (pPeer->billboard.id != 0) on::BillboardChange(event);
    modify_item_inventory(event, ::slot{static_cast<short>(auction.item), -1});
    pPeer->auction_item = 0;
    refresh_auction_huds(world->name);
    send_tile_update(event, ::gamePacket{.punch = auction.pos}, world->blocks[tile], *world);
}
