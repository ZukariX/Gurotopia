#include "pch.hpp"
#include "store.hpp"
#include "buy.hpp"
#include "onVariant/SetBux.hpp"
#include "database/shouhin.hpp"
#include <limits>
#include <iterator>

namespace
{
    int backpack_price(const ::peer &player)
    {
        if (player.slot_size < 16 || player.slot_size >= 396) return 0;
        const int level = (player.slot_size - 16) / 10 + 1;
        return 100 * level * level - 200 * level + 200;
    }

    short tab_for(const std::string &name)
    {
        const std::array<std::string_view, 5> tabs{"locks", "itempack", "bigitems", "weather", "token"};
        const auto found = std::ranges::find(tabs, name);
        return found == tabs.end() ? 0 : static_cast<short>(found - tabs.begin() + 1);
    }

    std::string description(short tab, const ::peer &player)
    {
        if (tab == 5) return std::format(
            "set_description_text|`2Spend your Growtokens!`` (You have `5{}``) You earn Growtokens from Crazy Jim and Sales-Man. Select the item you'd like more info on, or BACK to go back.\n",
            player.held(1486));
        const std::array<std::string_view, 4> names{"Locks And Stuff!", "Item Packs!", "Awesome Items!", "Weather Machines!"};
        return std::format("set_description_text|`2{}``  Select the item you'd like more info on, or BACK to go back.\n", names.at(tab - 1));
    }

    std::string tab_buttons(short tab)
    {
        const std::array<std::string_view, 5> names{"Locks And Stuff", "Item Packs", "Awesome Items", "Weather Machines", "Growtoken Items"};
        const std::array<std::string_view, 5> buttons{"locks_menu", "itempack_menu", "bigitems_menu", "weather_menu", "token_menu"};
        const std::array<int, 5> icons{1, 3, 4, 5, 2};
        std::string result = "enable_tabs|1\nadd_tab_button|main_menu|Home|interface/large/btn_shop.rttex||0|0|0|0||||-1|-1|||0|0|CustomParams:|\n";
        for (std::size_t i = 0; i < names.size(); ++i)
            result += std::format("add_tab_button|{}|{}|interface/large/btn_shop.rttex|{}|{}|{}|0|0||||-1|-1|||0|0|CustomParams:|\n",
                buttons[i], names[i], i == 3 ? "Tired of the same sunny sky?  We offer alternatives within..." : "",
                static_cast<int>(tab == i + 1), icons[i]);
        return result;
    }

    void show_tab(ENetEvent &event, short tab, std::string_view selection)
    {
        const ::peer &player = *static_cast<::peer*>(event.peer->data);
        const int backpack_cost = backpack_price(player);
        std::string request = description(tab, player) + tab_buttons(tab);
        for (const auto &product : shouhin_tachi)
        {
            const auto &entry = product.second;
            const short entry_tab = product.first;
            if (entry_tab != tab) continue;
            const bool upgrade = entry.btn == "upgrade_backpack";
            if (upgrade && backpack_cost == 0) continue;
            request += std::format("add_button|{}|{}|{}|{}|{}|{}|{}|0|||-1|-1||-1|-1||1||||||0|0|CustomParams:|\n",
                entry.btn, entry.name, entry.rttx, entry.description, entry.tex1, entry.tex2, upgrade ? backpack_cost : entry.cost);
        }
        if (!selection.empty()) request += std::format("select_item|{}\n", selection);
        send_varlist(event.peer, {"OnStoreRequest", request});
    }

    std::vector<short> item_pool(type kind, int low, int high)
    {
        std::vector<short> result;
        for (const ::item &item : items)
            if (item.type == kind && item.rarity >= low && item.rarity <= high) result.push_back(item.id);
        return result;
    }

    bool random_items(std::vector<::slot> &give, const std::vector<short> &pool, int count)
    {
        if (pool.empty()) return false;
        for (int i = 0; i < count; ++i) give.emplace_back(pool[rand() % pool.size()], 1);
        return true;
    }

    bool rewards(const shouhin &entry, std::vector<::slot> &give)
    {
        std::ranges::transform(entry.items, std::back_inserter(give), [](const auto &item) { return ::slot{item.first, item.second}; });
        if (entry.btn == "basic_splice")
        {
            give.emplace_back(11, 10);
            return random_items(give, {3567, 2793, 57, 13, 17, 21, 101, 381, 1139}, 10);
        }
        if (entry.btn == "rare_seed") return random_items(give, item_pool(type::SEED, 13, 60), 5);
        if (entry.btn == "clothes_pack") return random_items(give, item_pool(type::CLOTHING, 0, 10), 3);
        if (entry.btn == "rare_clothes_pack") return random_items(give, item_pool(type::CLOTHING, 11, 60), 3);
        return !give.empty();
    }

    int price(const shouhin &entry, const ::peer &player)
    {
        if (entry.btn == "upgrade_backpack") return backpack_price(player);
        if (entry.cost == std::numeric_limits<int>::min()) return -1;
        return std::abs(entry.cost);
    }

    bool pay_and_give(ENetEvent &event, const shouhin &entry, short tab, int cost, std::vector<::slot> &give)
    {
        if (entry.btn == "upgrade_backpack")
        {
            if (cost <= 0) return false;
            ::peer &player = *static_cast<::peer*>(event.peer->data);
            player.slot_size += 10;
            send_inventory_state(event);
            return true;
        }
        if (!rewards(entry, give)) return false;
        std::vector<::slot> take;
        if (tab == 5 && cost > 0) take.emplace_back(1486, static_cast<short>(cost));
        return exchange_items(event, take, give);
    }

    std::string received(const std::vector<::slot> &give)
    {
        std::string result;
        for (const auto &slot : give) result += std::format("{}, ", id_to_item(slot.id).raw_name);
        return result;
    }

    bool can_afford(ENetEvent &event, const shouhin &entry, int cost, int balance, std::string_view currency)
    {
        if (balance >= cost) return true;
        send_varlist(event.peer, {"OnStorePurchaseResult", std::format(
            "You can't afford `0{}``!  You're `${}`` {} short.", entry.name, cost - balance, currency)});
        return false;
    }

    void purchase(ENetEvent &event, const shouhin &entry, short tab)
    {
        ::peer &player = *static_cast<::peer*>(event.peer->data);
        const int cost = price(entry, player);
        if (cost < 0) return;
        const bool tokens = tab == 5;
        const int balance = tokens ? player.held(1486) : player.gems;
        const std::string_view currency = tokens ? "`2Growtokens``" : "Gems";
        if (!can_afford(event, entry, cost, balance, currency)) return;
        std::vector<::slot> give;
        if (!pay_and_give(event, entry, tab, cost, give))
        {
            send_varlist(event.peer, { "OnStorePurchaseResult", "This purchase could not be completed. Check your backpack space." });
            return;
        }
        if (!tokens) player.gems -= cost;
        send_varlist(event.peer, { "OnStorePurchaseResult", std::format(
            "You've purchased `0{}`` for `${}`` {}.\nYou have `${}`` {} left.\n\n`5Received: ```0{}``", entry.name, cost,
            currency, balance - cost, currency, received(give)) });
        if (!tokens) on::SetBux(event);
    }
}

void action::buy(ENetEvent &event, const std::string &header, const std::string_view selection)
{
    const std::string item = ::hPipe{header}["item"];
    if (item == "main") { action::store(event, ""); return; }
    const short tab = tab_for(item);
    if (tab != 0) { show_tab(event, tab, selection); return; }
    const auto found = std::ranges::find_if(shouhin_tachi, [&item](const auto &product) { return product.second.btn == item; });
    if (found != shouhin_tachi.end()) purchase(event, found->second, found->first);
}
