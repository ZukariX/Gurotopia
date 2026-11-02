#include "pch.hpp"

#include "database/mail.hpp"
#include "onVariant/ActiveAuctionUpdate.hpp"
#include "onVariant/EventButtonDataSet.hpp"

#include "auction.hpp"

#include <algorithm>
#include <ctime>
#include <memory>

std::vector<::auction_entry> auctions;

bool modify_world_locks(int uid, int amount)
{
    ::hStmt statement{"UPDATE peer SET wl_storage = wl_storage + ? WHERE uid = ? AND wl_storage + ? BETWEEN 0 AND 2147483647"};
    MYSQL_BIND params[]{make_bind_in(amount), make_bind_in(uid), make_bind_in(amount)};
    statement.bind_param(params);
    statement.execute();
    return mysql_stmt_errno(statement.pStmt) == 0 && mysql_stmt_affected_rows(statement.pStmt) == 1;
}

void auction_entry::update_block(::block &block) const
{
    block.auction_item = this->item;
    block.auction_end_time = this->end_time;
    block.auction_duration = this->duration;
    block.auction_price = this->price;
    block.auction_state = 1;
}

namespace
{
    void tile_update(::world &world, const ::pos &pos)
    {
        bool sent{};
        peers(world.name, PeerSearch::WORLD, [&](ENetPeer *peer)
        {
            if (sent) return;
            sent = true;
            ENetEvent event{.peer = peer};
            send_tile_update(event, ::gamePacket{.punch = pos}, world.blocks[cord(pos.x_int(), pos.y_int())], world);
        });
    }

    bool settle_auction(std::size_t index)
    {
        const ::auction_entry auction = auctions[index];
        auto loaded = std::ranges::find(worlds, auction.world, &::world::name);
        std::unique_ptr<::world> unloaded;
        if (loaded == worlds.end()) unloaded = std::make_unique<::world>(auction.world);
        ::world &world = unloaded ? *unloaded : *loaded;
        const int tile = cord(auction.pos.x_int(), auction.pos.y_int());
        if (tile < 0 || tile >= static_cast<int>(world.blocks.size())) return false;
        ::block &block = world.blocks[tile];
        const ::block previous = block;
        bool saved = mysql_query(db.mysql, "START TRANSACTION") == 0;
        const bool sold = auction.bidder > 0;
        for (const auto &[uid, amount] : auction.bids)
            if (uid != auction.bidder)
                saved = saved && modify_world_locks(uid, amount) && add_auction_mail(uid, "auctionfailbidder", 0, auction.world, amount);
        if (sold)
            saved = saved && modify_world_locks(auction.owner, static_cast<int>(auction.bid)) &&
                add_auction_mail(auction.owner, "auctionsuccessowner", auction.item, auction.world, static_cast<int>(auction.bid)) &&
                add_auction_mail(auction.bidder, "auctionsuccess", auction.item, auction.world, static_cast<int>(auction.bid));
        else saved = saved && add_auction_mail(auction.owner, "auctionfail", auction.item, auction.world, 0);
        block.auction_item = 0;
        block.auction_end_time = 0;
        block.auction_duration = 1;
        block.auction_price = 0;
        block.auction_state = 0;
        if (sold && block.fg == 15546/*Auction Block*/) block.fg = 15610/*Auction Block - Sold Out*/;
        auctions.erase(auctions.begin() + index);
        if (saved)
        {
            ::hStmt statement{"DELETE FROM auction WHERE id = ?"};
            MYSQL_BIND param = make_bind_in(auction.id);
            statement.bind_param(&param);
            statement.execute();
            saved = mysql_stmt_errno(statement.pStmt) == 0 && mysql_stmt_affected_rows(statement.pStmt) == 1;
        }
        saved = saved && world.mysql_update_all();
        if (!saved || mysql_query(db.mysql, "COMMIT"))
        {
            mysql_query(db.mysql, "ROLLBACK");
            auctions.insert(auctions.begin() + index, auction);
            block = previous;
            return false;
        }
        std::vector<ENetPeer*> notified{};
        peers("", PeerSearch::EVERYONE, [&](ENetPeer *peer)
        {
            ::peer *pOthers = static_cast<::peer*>(peer->data);
            if (!pOthers || pOthers->user_id <= 0) return;
            if (pOthers->user_id != auction.owner && !std::ranges::any_of(auction.bids,
                [&](const auto &bid) { return bid.first == pOthers->user_id; })) return;
            if (sold && pOthers->user_id == auction.owner) pOthers->wl_storage += static_cast<int>(auction.bid);
            for (const auto &[uid, amount] : auction.bids)
                if (uid != auction.bidder && uid == pOthers->user_id) pOthers->wl_storage += amount;
            notified.push_back(peer);
        });
        refresh_auction_huds(auction.world);
        tile_update(world, auction.pos);
        for (ENetPeer *peer : notified) send_mailbox_button(peer);
        return true;
    }
}

bool auction_entry::mysql_update()
{
    ::blob blob{};
    for (const auto &[uid, amount] : this->bids)
    {
        blob.i32(uid);
        blob.i32(amount);
    }
    const int x = this->pos.x_int(), y = this->pos.y_int();
    const u_int item = this->item, duration = this->duration;
    ::hStmt statement{"REPLACE INTO auction (id, world, x, y, owner, item, price, bid, duration, end_time, bidder, bids) VALUES (NULLIF(?, 0), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"};
    MYSQL_BIND params[]{make_bind_in(this->id), make_bind_in(this->world), make_bind_in(x), make_bind_in(y),
        make_bind_in(this->owner), make_bind_in(item), make_bind_in(this->price), make_bind_in(this->bid),
        make_bind_in(duration), make_bind_in(this->end_time), make_bind_in(this->bidder), make_bind_in(blob)};
    statement.bind_param(params);
    statement.execute();
    if (mysql_stmt_errno(statement.pStmt)) return false;
    if (this->id == 0) this->id = static_cast<u_int>(mysql_stmt_insert_id(statement.pStmt));
    return true;
}

void load_auctions()
{
    ::hStmt statement{"SELECT id, world, x, y, owner, item, price, bid, duration, end_time, bidder, bids FROM auction ORDER BY id"};
    ::auction_entry auction{};
    ::blob blob{};
    int x{}, y{};
    u_int item{}, duration{};
    u_long world_length{}, bids_length{};
    MYSQL_BIND result[]{make_bind_out(auction.id), make_bind_out(auction.world), make_bind_out(x), make_bind_out(y),
        make_bind_out(auction.owner), make_bind_out(item), make_bind_out(auction.price), make_bind_out(auction.bid),
        make_bind_out(duration), make_bind_out(auction.end_time), make_bind_out(auction.bidder), make_bind_out(blob)};
    result[1].length = &world_length;
    result[11].length = &bids_length;
    if (mysql_stmt_bind_result(statement.pStmt, result) || mysql_stmt_execute(statement.pStmt))
        throw std::runtime_error(mysql_stmt_error(statement.pStmt));
    auctions.clear();
    int status{};
    while ((status = mysql_stmt_fetch(statement.pStmt)) == 0)
    {
        auction.world.resize(world_length);
        auction.pos = ::pos{x, y};
        auction.item = static_cast<u_short>(item);
        auction.duration = static_cast<u_char>(duration);
        auction.bids.clear();
        blob.resize(bids_length);
        int offset{};
        while (offset < static_cast<int>(blob.size()))
        {
            int uid{}, amount{};
            if (!blob.read_i32(uid, offset) || !blob.read_i32(amount, offset) || uid <= 0 || amount <= 0)
                throw std::runtime_error("Invalid saved auction bid.");
            auction.bids.emplace_back(uid, amount);
        }
        auctions.push_back(auction);
        auction.world.resize(result[1].buffer_length);
        blob.resize(result[11].buffer_length);
    }
    if (status != MYSQL_NO_DATA) throw std::runtime_error(mysql_stmt_error(statement.pStmt));
}

void restore_auctions(::world &world)
{
    for (const ::auction_entry &auction : auctions)
    {
        if (auction.world != world.name) continue;
        const int tile = cord(auction.pos.x_int(), auction.pos.y_int());
        if (tile >= 0 && tile < static_cast<int>(world.blocks.size()) && world.blocks[tile].fg == 15546/*Auction Block*/)
            auction.update_block(world.blocks[tile]);
    }
}

void update_auctions()
{
    const auto now = std::time(nullptr);
    for (std::size_t i = 0; i < auctions.size();)
        if (auctions[i].end_time <= now && settle_auction(i)) continue;
        else ++i;
}

::auction_entry *viewed_auction(const ::peer &peer)
{
    ::auction_entry *first{};
    for (::auction_entry &auction : auctions)
    {
        if (auction.world != peer.recent_worlds.back()) continue;
        if (peer.auction_world == auction.world && peer.auction_pos == auction.pos) return &auction;
        if (!first || auction.end_time < first->end_time) first = &auction;
    }
    return first;
}

void send_auction_hud(ENetPeer *peer)
{
    const ::peer *pPeer = static_cast<::peer*>(peer->data);
    const ::auction_entry *auction = viewed_auction(*pPeer);
    std::string price = auction ? std::to_string(auction->bid) : "";
    for (int i = static_cast<int>(price.size()) - 3; i > 0; i -= 3) price.insert(i, ",");
    ENetEvent event{.peer = peer};
    on::EventButtonDataSet(event, true, auction ? price + " WL" : "");
    on::EventButtonDataSet(event, false, auction ? price + " WL" : "");
    on::ActiveAuctionUpdate(peer, auction);
}

void refresh_auction_huds(const std::string &world)
{
    peers(world, PeerSearch::WORLD, [](ENetPeer *peer) { send_auction_hud(peer); });
}
