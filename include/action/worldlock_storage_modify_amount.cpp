#include "pch.hpp"

#include "database/auction.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "onVariant/WorldLockStorageResponse.hpp"
#include "tools/conversions.hpp"
#include "worldlock_storage_modify_amount.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace
{
    struct denomination { short id; int value; const char *name; };
    constexpr denomination locks[3]{
        {7188, BLUE_GEM_LOCK_VALUE, "Blue Gem Lock"}, {1796, DIAMOND_LOCK_VALUE, "Diamond Lock"}, {242, 1, "World Lock"}
    };
    struct operation { const char *type; int sign; const char *verb; };
    constexpr operation operations[2]{
        {"1", -1, "deposited"}, {"-1", 1, "collected"}
    };

    bool deposit(int amount, const int (&held)[3], int (&changes)[3])
    {
        int spare = -1;
        for (int i = 0; i < 3; ++i)
        {
            changes[i] = -std::min(held[i], amount / locks[i].value);
            amount += changes[i] * locks[i].value;
            if (held[i] + changes[i] > 0) spare = i;
        }
        if (amount > 0)
        {
            if (spare < 0) return false;
            --changes[spare];
            amount -= locks[spare].value;
        }
        int change = -amount;
        for (int i = 0; i < 3; ++i)
        {
            changes[i] += change / locks[i].value;
            change %= locks[i].value;
        }
        return true;
    }

    void withdraw(int amount, const int (&held)[3], int (&changes)[3], int free)
    {
        for (int i = 0; i < 3; ++i)
        {
            const int count = std::min(amount / locks[i].value, 200);
            amount -= count * locks[i].value;
            if (count == 0) continue;
            if (held[i] == 0 && free <= 0) continue;
            changes[i] = std::min(count, 200 - held[i]);
            if (held[i] == 0) --free;
        }
    }

    bool fits(const int (&held)[3], const int (&changes)[3], int free)
    {
        for (int i = 0; i < 3; ++i)
        {
            const int count = held[i] + changes[i];
            if (count < 0 || count > 200) return false;
            free += (held[i] > 0) - (count > 0);
        }
        return free >= 0;
    }

    void apply(ENetEvent &event, const int (&changes)[3], const operation &op)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        int value{};
        for (int i = 0; i < 3; ++i)
            value += changes[i] * locks[i].value;
        if (value == 0) return;
        const std::vector<::slot> before = pPeer->slots;
        const int balance = pPeer->wl_storage;
        for (int i = 0; i < 3; ++i)
            if (changes[i] != 0) pPeer->emplace({locks[i].id, static_cast<short>(changes[i])});
        std::erase_if(pPeer->slots, [](const ::slot &slot) { return slot.count == 0; });
        pPeer->wl_storage -= value;
        if (!pPeer->mysql_update_all())
        {
            pPeer->slots = before;
            pPeer->wl_storage = balance;
            return;
        }
        std::vector<std::string> parts;
        for (int i = 0; i < 3; ++i)
        {
            if (changes[i] == 0) continue;
            const u_int count = changes[i] < 0 ? -changes[i] : changes[i];
            send_data(*event.peer, compress_state(::gamePacket{
                .type = static_cast<int>((count << (changes[i] < 0 ? 16 : 24)) | 0x0du), .id = locks[i].id
            }));
            if (changes[i] > 0) parts.push_back(std::format("{} `2{}s``", changes[i], locks[i].name));
        }
        if (op.sign > 0)
        {
            const std::string message = std::format("You {} {}.", op.verb, join(parts, " and "));
            send_varlist(event.peer, { "OnTalkBubble", static_cast<u_int>(pPeer->netid), message, 0u, 1u });
            on::ConsoleMessage(event.peer, message);
        }
        refresh_auction_huds(pPeer->recent_worlds.back());
        on::WorldLockStorageResponse(event.peer);
    }

    void transfer(ENetEvent &event, int amount, const operation &op)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        if (op.sign < 0 && amount > std::numeric_limits<int>::max() - pPeer->wl_storage) return;
        int held[3]{}, changes[3]{};
        for (int i = 0; i < 3; ++i)
        {
            const auto it = std::ranges::find(pPeer->slots, locks[i].id, &::slot::id);
            if (it != pPeer->slots.end()) held[i] = it->count;
        }
        const int free = pPeer->slot_size - std::ranges::count_if(pPeer->slots, [](const ::slot &slot) { return slot.count > 0; });
        if (op.sign < 0)
        {
            if (!deposit(amount, held, changes)) return;
        }
        else withdraw(std::min(amount, pPeer->wl_storage), held, changes, free);
        if (!fits(held, changes, free)) return;
        apply(event, changes, op);
    }
}

void action::worldlock_storage_modify_amount(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->user_id <= 0 || pPeer->wl_storage < 0) return;
    const ::hPipe hPipe{header};
    const std::string input = hPipe["amount"];
    int amount{};
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), amount);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size() || amount <= 0) return;
    const auto op = std::ranges::find_if(operations, [&hPipe](const operation &entry) { return hPipe["type"] == entry.type; });
    if (op != std::end(operations)) transfer(event, amount, *op);
}
