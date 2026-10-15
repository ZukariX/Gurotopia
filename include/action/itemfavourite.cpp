#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "itemfavourite.hpp"

void action::itemfavourite(ENetEvent& event, const std::string& header)
{
    const auto fields = readch(header, '|');
    if (fields.size() < 5) return;
    short id{};
    if (!to_item_id(fields[4], id)) return;

    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    auto it = std::ranges::find(pPeer->fav, id);
    bool fav = it != pPeer->fav.end();
    if (pPeer->fav.size() >= 20 && !fav)
    {
        const std::string &message = "You cannot favorite any more items. Remove some from your list and try again.";

        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, message, 0u, 1u });
        on::ConsoleMessage(event.peer, message);
        return;
    }
    send_varlist(event.peer, { "OnFavItemUpdated", id, (fav) ? 0 : 1 });
    if (fav) pPeer->fav.erase(it);
    else pPeer->fav.emplace_back(static_cast<short>(id));
}
