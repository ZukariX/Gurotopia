#include "pch.hpp"

#include "database/auction.hpp"
#include "auction_getpage.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <limits>
#include <utility>

void action::auction_getpage(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->user_id <= 0) return;

    const ::hPipe hPipe{header};
    int page{}, sort{}, search{};
    for (const auto &field : {std::pair{"pageIndex", &page}, std::pair{"sortType", &sort}, std::pair{"searchItem", &search}})
    {
        const std::string input = hPipe[field.first];
        const auto parsed = std::from_chars(input.data(), input.data() + input.size(), *field.second);
        if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size()) return;
    }
    if (page < -1 || page == std::numeric_limits<int>::max() || sort < 0 || sort > 3 || search < 0) return;

    update_auctions();
    std::vector<const ::auction_entry*> list{};
    const std::time_t now = std::time(nullptr);
    for (const ::auction_entry &auction : auctions)
        if (auction.end_time > now && (search == 0 || auction.item == search))
            list.push_back(&auction);
    std::ranges::stable_sort(list, [sort](const ::auction_entry *left, const ::auction_entry *right)
    {
        const u_int a = sort < 2 ? left->bid : left->end_time;
        const u_int b = sort < 2 ? right->bid : right->end_time;
        return sort % 2 == 0 ? a < b : a > b;
    });

    ++page;
    const std::size_t first = static_cast<std::size_t>(page) * 5/*items*/;
    std::string data{"{\"Items\":["};
    for (std::size_t i = first; i < std::min(first + 5/*items*/, list.size()); ++i)
    {
        if (i != first) data += ',';
        const ::auction_entry &auction = *list[i];
        data += std::format("{{\"bid\":{},\"endTime\":{},\"id\":{},\"world\":\"{}\"}}", auction.bid, auction.end_time, auction.item, auction.world);
    }
    data += "]}";
    send_varlist(event.peer, { "OnRequestAuctionPage", 1, page, static_cast<int>(list.size()), data });
}
