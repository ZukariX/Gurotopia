#include "pch.hpp"
#include "inventory.hpp"
#include <limits>

namespace inventory
{
    bool valid_id(int id)
    {
        return id > 0 && id <= std::numeric_limits<short>::max() && static_cast<std::size_t>(id) < items.size();
    }

    int count(const std::vector<::slot> &slots, int id)
    {
        const auto found = std::ranges::find(slots, id, &::slot::id);
        return found == slots.end() ? 0 : found->count;
    }

    short remove(std::vector<::slot> &slots, const ::slot &request)
    {
        const auto found = std::ranges::find(slots, request.id, &::slot::id);
        if (found == slots.end() || found->count <= 0) return 0;
        const int amount = std::min(-static_cast<int>(request.count), static_cast<int>(found->count));
        found->count -= amount;
        return static_cast<short>(-amount);
    }

    bool has_room(const std::vector<::slot> &slots, int capacity, int held)
    {
        if (held > 0) return true;
        return std::ranges::count_if(slots, [](const ::slot &item) { return item.count > 0; }) < capacity;
    }

    short add(std::vector<::slot> &slots, int capacity, const ::slot &request)
    {
        const int held = count(slots, request.id);
        if (held < 0 || held >= 200) return 0;
        if (!has_room(slots, capacity, held)) return 0;
        const short amount = static_cast<short>(std::min<int>(request.count, 200 - held));
        const auto found = std::ranges::find(slots, request.id, &::slot::id);
        if (found == slots.end()) slots.emplace_back(request.id, amount);
        else found->count += amount;
        return amount;
    }

    short change(std::vector<::slot> &slots, int capacity, const ::slot &request)
    {
        if (!valid_id(request.id) || request.count == 0) return 0;
        return request.count < 0 ? remove(slots, request) : add(slots, capacity, request);
    }

    bool apply_offer(std::vector<::slot> &slots, int capacity, const std::vector<::slot> &offer, bool taking)
    {
        for (const ::slot &item : offer)
        {
            if (item.count <= 0 || item.count > 200) return false;
            const short amount = taking ? -item.count : item.count;
            if (change(slots, capacity, ::slot{item.id, amount}) != amount) return false;
        }
        return true;
    }

    bool plan(const std::vector<::slot> &slots, int capacity, const std::vector<::slot> &take,
              const std::vector<::slot> &give, std::vector<::slot> &after)
    {
        after = slots;
        return apply_offer(after, capacity, take, true) && apply_offer(after, capacity, give, false);
    }
}
