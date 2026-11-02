#include "pch.hpp"

#include "EventButtonDataSet.hpp"

void on::EventButtonDataSet(ENetEvent& event, bool active, const std::string &price)
{
    const bool visible = active ? price.empty() : !price.empty();
    send_varlist(event.peer, { 
        "OnEventButtonDataSet",
        (active) ? "AuctionButton" : "ActiveAuctionButton",
        visible ? 1 : 0,
        "{" +
            std::format(
                "\"active\":{},\"buttonAction\":\"{}\",\"buttonState\":0,\"buttonTemplate\":\"{}\",\"counter\":0,\"counterMax\":{},\"itemIdIcon\":0,\"name\":\"{}\",\"notification\":0,\"order\":30,\"rcssClass\":\"{}\",\"text\":\"{}\",\"visibilityFlag\":{}",
                visible ? "true" : "false",
                (active) ? "show_auction_ui" : "", 
                (active) ? "BaseEventButton" : "ActiveAuctionEventButton",
                (active) ? "0" : "20",
                (active) ? "AuctionButton" : "ActiveAuctionButton",
                (active) ? "auction" : "activeauction", 
                active ? (visible ? "Find Auction" : "") : price,
                (active) ? "10" : "0") 
        + "}"
    });
}
