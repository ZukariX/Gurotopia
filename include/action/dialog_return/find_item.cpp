#include "pch.hpp"

#include "onVariant/SetClothing.hpp"

#include "find_item.hpp"

void find_item(ENetEvent& event, const ::hPipe &hPipe)
{
    const auto fields = readch(hPipe["buttonClicked"], '_');
    if (fields.size() < 2) return;
    if (fields[0] != "searchableItemListButton") return;
    short id{};
    if (!to_item_id(fields[1], id)) return;
    
    modify_item_inventory(event, ::slot(static_cast<short>(id), 200));
}
