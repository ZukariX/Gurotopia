#include "pch.hpp"

#include "database/auction.hpp"
#include "WorldLockStorageResponse.hpp"

#include <algorithm>
#include <limits>

void on::WorldLockStorageResponse(ENetPeer *peer)
{
    const ::peer *pPeer = static_cast<::peer*>(peer->data);
    std::vector<std::pair<const ::auction_entry*, int>> bids;
    unsigned long long locked{};
    for (const ::auction_entry &auction : auctions)
    {
        const auto bid = std::ranges::find(auction.bids, pPeer->user_id, &std::pair<int, int>::first);
        if (bid == auction.bids.end() || bid->second <= 0) continue;
        bids.emplace_back(&auction, bid->second);
        locked += bid->second;
    }
    std::ranges::stable_sort(bids, [](const auto &left, const auto &right) { return left.first->end_time < right.first->end_time; });
    std::string data{"\x95", 1}; // @note msgpack [bids, balance, 0, locked, 0]
    auto integer = [&data](unsigned long long value)
    {
        struct encoding { unsigned long long maximum; u_char prefix; int bytes; };
        constexpr encoding formats[]{
            {0x7f, 0, 1}, {0xff, 0xcc, 1}, {0xffff, 0xcd, 2},
            {0xffffffffu, 0xce, 4}, {std::numeric_limits<unsigned long long>::max(), 0xcf, 8}
        };
        const auto format = std::ranges::find_if(formats, [value](const encoding &entry) { return value <= entry.maximum; });
        if (format->prefix != 0) data.push_back(static_cast<char>(format->prefix));
        for (int i = format->bytes - 1; i >= 0; --i) data.push_back(static_cast<char>(value >> (i * 8)));
    };
    if (bids.size() < 16) data.push_back(static_cast<char>(0x90 + bids.size()));
    else
    {
        const int bytes = bids.size() <= 0xffff ? 2 : 4;
        data.push_back(bytes == 2 ? '\xdc' : '\xdd');
        for (int i = bytes - 1; i >= 0; --i) data.push_back(static_cast<char>(bids.size() >> (i * 8)));
    }
    for (const auto &[auction, amount] : bids)
    {
        data.push_back('\x94');
        integer(auction->item);
        integer(amount);
        integer(auction->end_time);
        data.push_back(static_cast<char>(0xa0 + auction->world.size()));
        data.append(auction->world);
    }
    integer(pPeer->wl_storage);
    data.push_back('\0');
    integer(locked);
    data.push_back('\0');
    send_varlist(peer, { "OnWorldLockStorageResponse", data });
}
