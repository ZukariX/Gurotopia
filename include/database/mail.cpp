#include "pch.hpp"

#include "onVariant/BillboardChange.hpp"
#include "mail.hpp"

#include <algorithm>
#include <ctime>

namespace
{
    void integer(std::string &data, u_int value)
    {
        if (value <= 0x7f) data.push_back(static_cast<char>(value));
        else
        {
            const int bytes = value <= 0xff ? 1 : value <= 0xffff ? 2 : 4;
            data.push_back(bytes == 1 ? '\xcc' : bytes == 2 ? '\xcd' : '\xce');
            for (int i = bytes - 1; i >= 0; --i) data.push_back(static_cast<char>(value >> (i * 8)));
        }
    }

    void text(std::string &data, const std::string &value)
    {
        if (value.size() < 32) data.push_back(static_cast<char>(0xa0 | value.size()));
        else
        {
            const int bytes = value.size() <= 0xff ? 1 : value.size() <= 0xffff ? 2 : 4;
            data.push_back(bytes == 1 ? '\xd9' : bytes == 2 ? '\xda' : '\xdb');
            for (int i = bytes - 1; i >= 0; --i) data.push_back(static_cast<char>(value.size() >> (i * 8)));
        }
        data.append(value);
    }

    void array(std::string &data, std::size_t size)
    {
        if (size < 16) data.push_back(static_cast<char>(0x90 | size));
        else
        {
            const int bytes = size <= 0xffff ? 2 : 4;
            data.push_back(bytes == 2 ? '\xdc' : '\xdd');
            for (int i = bytes - 1; i >= 0; --i) data.push_back(static_cast<char>(size >> (i * 8)));
        }
    }

    bool reward(const ::mail &mail)
    {
        return mail.kind == "auctionsuccess" || mail.kind == "auctionfail";
    }
}

bool add_auction_mail(int uid, const std::string &kind, u_short item, const std::string &world, int amount)
{
    if (uid <= 0 || amount < 0 || world.size() > 24) return false;
    if (kind != "auctionsuccess" && kind != "auctionsuccessowner" && kind != "auctionfail" && kind != "auctionfailbidder") return false;
    const u_int item_id = item;
    const int days = kind == "auctionfailbidder" ? 1 : kind == "auctionsuccessowner" ? 7 : 30;
    const u_int expires = static_cast<u_int>(std::time(nullptr) + days * 24 * 3600);
    ::hStmt hStmt{ "INSERT INTO mail (uid, kind, item, world, amount, expires) VALUES (?, ?, ?, ?, ?, ?)" };
    MYSQL_BIND params[]{ make_bind_in(uid), make_bind_in(kind), make_bind_in(item_id), make_bind_in(world), make_bind_in(amount), make_bind_in(expires) };
    return !mysql_stmt_bind_param(hStmt.pStmt, params) && !mysql_stmt_execute(hStmt.pStmt);
}

bool load_mail(int uid, std::vector<::mail> &mails)
{
    mails.clear();
    ::hStmt hStmt{ "SELECT id, kind, item, world, amount, expires, is_read, claimed FROM mail WHERE uid = ? AND expires > UNIX_TIMESTAMP() ORDER BY id DESC" };
    MYSQL_BIND param = make_bind_in(uid);
    ::mail mail{ .uid = uid };
    u_long lengths[2]{};
    MYSQL_BIND result[]{ make_bind_out(mail.id), make_bind_out(mail.kind), make_bind_out(mail.item), make_bind_out(mail.world),
                         make_bind_out(mail.amount), make_bind_out(mail.expires), make_bind_out(mail.is_read), make_bind_out(mail.claimed) };
    result[1].length = &lengths[0];
    result[3].length = &lengths[1];
    if (mysql_stmt_bind_param(hStmt.pStmt, &param) || mysql_stmt_bind_result(hStmt.pStmt, result) ||
        mysql_stmt_execute(hStmt.pStmt) || mysql_stmt_store_result(hStmt.pStmt)) return false;
    int status{};
    while ((status = mysql_stmt_fetch(hStmt.pStmt)) == 0)
    {
        mail.kind.resize(lengths[0]);
        mail.world.resize(lengths[1]);
        mails.push_back(mail);
        mail.kind.resize(result[1].buffer_length);
        mail.world.resize(result[3].buffer_length);
    }
    return status == MYSQL_NO_DATA;
}

std::string mail_data(const std::vector<::mail> &mails)
{
    std::string data{ "\x93", 1 };
    array(data, mails.size());
    for (const ::mail &mail : mails)
    {
        const bool item_reward = reward(mail);
        const ::item &item = id_to_item(static_cast<u_short>(mail.item));
        const std::string name = item.raw_name;
        std::string title{}, description{};
        if (mail.kind == "auctionsuccess")
        {
            title = std::format("{}|{}|", name, mail.amount);
            description = std::format("{}|{}|{}|", name, mail.world, mail.amount);
        }
        else if (mail.kind == "auctionfail") title = description = std::format("{}|{}|", name, mail.world);
        else if (mail.kind == "auctionsuccessowner") title = description = std::format("{}|{}|{}|", name, mail.amount, mail.world);
        else description = std::format("{}|", mail.amount);
        array(data, 13);
        integer(data, mail.id);
        integer(data, mail.uid);
        integer(data, mail.kind == "auctionfail" ? 1432 : mail.kind == "auctionfailbidder" ? 660 : 15610);
        text(data, std::format("[mail_{}_title]", mail.kind));
        text(data, std::format("[mail_{}_desc]", mail.kind));
        text(data, std::format("[mail_{}_body]", mail.kind));
        text(data, item_reward ? std::format("[{{\"{}\":1}}]", mail.item) : "[]");
        integer(data, item_reward ? 2 : 1);
        integer(data, mail.expires);
        integer(data, mail.is_read);
        text(data, "");
        text(data, title);
        text(data, description);
    }
    data.push_back(mails.empty() ? '\xfe' : '\0');
    integer(data, static_cast<u_int>(mails.size()));
    return data;
}

void send_mailbox_button(ENetPeer *peer)
{
    const ::peer *pPeer = static_cast<::peer*>(peer->data);
    if (pPeer->user_id <= 0) return;
    ::hStmt hStmt{ "SELECT COUNT(*) FROM mail WHERE uid = ? AND is_read = 0 AND expires > UNIX_TIMESTAMP()" };
    MYSQL_BIND param = make_bind_in(pPeer->user_id);
    int unread{};
    MYSQL_BIND result = make_bind_out(unread);
    if (mysql_stmt_bind_param(hStmt.pStmt, &param) || mysql_stmt_bind_result(hStmt.pStmt, &result) ||
        mysql_stmt_execute(hStmt.pStmt) || mysql_stmt_fetch(hStmt.pStmt)) return;
    send_varlist(peer, { "OnEventButtonDataSet", "MailboxButton", 1,
        std::format("{{\"active\":true,\"buttonAction\":\"show_mailbox_ui\",\"buttonState\":{},\"buttonTemplate\":\"BaseEventButton\",\"counter\":0,\"counterMax\":0,\"itemIdIcon\":0,\"name\":\"MailboxButton\",\"notification\":{},\"order\":30,\"rcssClass\":\"mailbox\",\"text\":\"\",\"visibilityFlag\":10}}", unread > 0 ? 1 : 0, unread) });
}

bool mark_mail_read(int uid, u_int id)
{
    ::hStmt hStmt{ "UPDATE mail SET is_read = 1 WHERE uid = ? AND id = ? AND expires > UNIX_TIMESTAMP()" };
    MYSQL_BIND params[]{ make_bind_in(uid), make_bind_in(id) };
    return !mysql_stmt_bind_param(hStmt.pStmt, params) && !mysql_stmt_execute(hStmt.pStmt);
}

bool claim_mail_rewards(ENetEvent &event, std::vector<u_int> &ids)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    std::vector<::mail> mails{}, selected{};
    if (!load_mail(pPeer->user_id, mails)) return false;
    std::vector<::slot> slots = pPeer->slots;
    for (u_int id : ids)
    {
        const auto mail = std::ranges::find(mails, id, &::mail::id);
        if (mail == mails.end() || mail->claimed || !reward(*mail) ||
            std::ranges::find(selected, id, &::mail::id) != selected.end()) continue;
        const ::item &item = id_to_item(static_cast<u_short>(mail->item));
        if (item.id == 0 || item.id != mail->item) return false;
        auto slot = std::ranges::find(slots, item.id, &::slot::id);
        if (slot != slots.end())
        {
            if (slot->count >= 200 || slot->count < 0) return false;
            ++slot->count;
        }
        else
        {
            if (std::ranges::count_if(slots, [](const ::slot &slot) { return slot.count > 0; }) >= pPeer->slot_size) return false;
            slots.emplace_back(item.id, 1);
        }
        selected.push_back(*mail);
    }
    if (selected.empty() || mysql_query(db.mysql, "START TRANSACTION")) return false;
    for (const ::mail &mail : selected)
    {
        ::hStmt hStmt{ "UPDATE mail SET claimed = 1, is_read = 1 WHERE uid = ? AND id = ? AND claimed = 0 AND expires > UNIX_TIMESTAMP()" };
        MYSQL_BIND params[]{ make_bind_in(pPeer->user_id), make_bind_in(mail.id) };
        if (mysql_stmt_bind_param(hStmt.pStmt, params) || mysql_stmt_execute(hStmt.pStmt) || mysql_stmt_affected_rows(hStmt.pStmt) != 1)
        {
            mysql_rollback(db.mysql);
            return false;
        }
    }
    slots.swap(pPeer->slots);
    if (!pPeer->mysql_update_all() || mysql_commit(db.mysql))
    {
        mysql_rollback(db.mysql);
        slots.swap(pPeer->slots);
        return false;
    }
    ids.clear();
    for (const ::mail &mail : selected)
    {
        ids.push_back(mail.id);
        send_data(*event.peer, compress_state(::gamePacket{ .type = 0x0100000d, .id = static_cast<int>(mail.item) }));
        if (pPeer->billboard.id != 0) on::BillboardChange(event);
        const ::item &item = id_to_item(static_cast<u_short>(mail.item));
        const std::string rarity = item.rarity < 999 ? std::format(" Rarity: `w{}``", item.rarity) : "";
        send_varlist(event.peer, { "OnConsoleMessage", std::format("Given `w 1 {}``.{}", item.raw_name, rarity) });
    }
    send_mailbox_button(event.peer);
    return true;
}
