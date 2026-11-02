#include "pch.hpp"

#include "database/auction.hpp"
#include "tools/create_dialog.hpp"
#include "set_auction_tile.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

void action::set_auction_tile(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->user_id <= 0 || pPeer->netid == 0 || pPeer->auction_world.empty() ||
        pPeer->auction_world != pPeer->recent_worlds.back() || !pPeer->trade_offers.empty()) return;

    const ::hPipe hPipe{header};
    int item{}, price{}, duration{}, state{};
    for (const auto &field : {std::pair{"item", &item}, std::pair{"price", &price}, std::pair{"duration", &duration}, std::pair{"state", &state}})
    {
        const std::string input = hPipe[field.first];
        const auto parsed = std::from_chars(input.data(), input.data() + input.size(), *field.second);
        if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size()) return;
    }
    if (item <= 0 || item >= static_cast<int>(items.size()) || price <= 0 || price > 9999999/*WL*/ || state != 1) return;
    if (duration != 1 && duration != 3 && duration != 12) return;
    const ::item &auction_item = id_to_item(static_cast<u_short>(item));
    if (auction_item.cat & CAT_UNTRADEABLE) return;
    if (std::ranges::none_of(pPeer->slots, [item](const ::slot &slot) { return slot.id == item && slot.count > 0; })) return;

    auto world = std::ranges::find(worlds, pPeer->auction_world, &::world::name);
    if (world == worlds.end()) return;
    if (world->owner != pPeer->user_id) return;
    if (pPeer->auction_pos.x < 0 || pPeer->auction_pos.x >= 100/*tiles*/ || pPeer->auction_pos.y < 0) return;
    const std::size_t index = static_cast<std::size_t>(cord(pPeer->auction_pos.x, pPeer->auction_pos.y));
    if (index >= world->blocks.size()) return;
    ::block &block = world->blocks[index];
    if (block.fg != 15546/*Auction Block*/ || block.auction_state != 0) return;
    if (std::ranges::any_of(auctions, [&pPeer](const ::auction_entry &auction)
        { return auction.world == pPeer->auction_world && auction.pos == pPeer->auction_pos; })) return;

    pPeer->auction_item = static_cast<u_short>(item);
    pPeer->auction_price = static_cast<u_int>(price);
    pPeer->auction_duration = static_cast<u_char>(duration);
    block.auction_item = pPeer->auction_item;
    block.auction_price = pPeer->auction_price;
    block.auction_duration = pPeer->auction_duration;
    block.auction_end_time = 0;
    block.auction_state = 0;

    send_varlist(event.peer, {
        "OnDialogRequest",
        ::create_dialog()
            .set_default_color("`o")
            .add_label_with_icon("big", "`wStart Auction``   ", 15546/*Auction Block*/)
            .add_spacer("small")
            .add_label_with_icon("small", auction_item.raw_name, item)
            .add_spacer("small")
            .add_smalltext(std::format("Duration: {} Hr", duration))
            .add_spacer("small")
            .add_smalltext(std::format("Starting Price: {} WL", price))
            .add_spacer("small")
            .end_dialog("auction_start_prompt", "Cancel", "Start") + "add_quick_exit|\n"
    }, -1, 100/*ms*/);
    send_tile_update(event, ::gamePacket{.punch = pPeer->auction_pos}, block, *world);
}
