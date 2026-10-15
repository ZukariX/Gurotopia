#include "pch.hpp"
#include "saved_state.hpp"
#include <limits>

namespace saved_state
{
    namespace
    {
        void text(::blob &data, const std::string &value)
        {
            if (value.size() > 32767) throw std::length_error("Saved text is too long");
            data.i16(static_cast<short>(value.size()));
            for (unsigned char c : value) data.u8(c);
        }

        void finish(const ::blob &data)
        {
            if (data.size() > 65535) throw std::length_error("Saved state is too large");
        }

        void version(const ::blob &data, int &pos)
        {
            u_int value{};
            data.read_u32(value, pos);
            if (value != 1) throw std::runtime_error("Unsupported saved state version");
        }

        short count(const ::blob &data, int &pos, int limit)
        {
            short result{};
            data.read_i16(result, pos);
            if (result < 0 || result > limit) throw std::out_of_range("Invalid saved entry count");
            return result;
        }

        int coordinate(float value, int limit)
        {
            if (!std::isfinite(value) || value < 0 || value >= limit || std::floor(value) != value)
                throw std::out_of_range("Invalid saved coordinate");
            return static_cast<int>(value);
        }

        int item_id(float value)
        {
            if (!std::isfinite(value) || value < 0 || value > 32767 || std::floor(value) != value) throw std::out_of_range("Invalid saved item");
            return static_cast<int>(value);
        }

        bool owned_clothing(const ::peer &player, int id, int kind)
        {
            if (id == 0) return true;
            const auto owned = std::ranges::find(player.slots, id, &::slot::id);
            if (owned == player.slots.end() || owned->count <= 0) return false;
            return id_to_item(id).cloth_type == kind;
        }

        void player_header(::peer &player, const ::blob &data, int &pos)
        {
            data.read_i32(player.gems, pos);
            data.read_u32(player.skin_color, pos);
            data.read_u32(player.hair_color, pos);
            for (auto &level : player.level)
            {
                short value{};
                data.read_i16(value, pos);
                level = static_cast<u_short>(value);
            }
            if (player.gems < 0 || player.level[0] < 1 || player.level[0] > 125)
                throw std::out_of_range("Invalid saved player state");
        }

        void clothes(::peer &player, const ::blob &data, int &pos)
        {
            const short size = count(data, pos, player.clothing.size());
            for (int i = 0; i < size; ++i)
            {
                int id{};
                data.read_i32(id, pos);
                if (id < 0 || id > 32767 || static_cast<std::size_t>(id) >= items.size()) throw std::out_of_range("Invalid saved clothing");
                if (!owned_clothing(player, id, i)) continue;
                player.clothing[i] = id;
            }
        }

        void favorites(::peer &player, const ::blob &data, int &pos)
        {
            const short size = count(data, pos, 20);
            for (int i = 0; i < size; ++i)
            {
                short id{};
                data.read_i16(id, pos);
                if (id <= 0 || static_cast<std::size_t>(id) >= items.size()) throw std::out_of_range("Invalid saved favorite");
                player.fav.push_back(id);
            }
        }

        ::pos position(const ::blob &data, int &pos)
        {
            int x{}, y{};
            data.read_i32(x, pos);
            data.read_i32(y, pos);
            if (x < 0 || x >= 100 || y < 0 || y >= 60) throw std::out_of_range("Invalid saved tile position");
            return ::pos{x, y};
        }

        void doors(::world &world, const ::blob &data, int &pos)
        {
            const short size = count(data, pos, 6000);
            for (int i = 0; i < size; ++i)
            {
                const auto at = position(data, pos);
                std::string destination, id;
                data.read_string(destination, pos);
                data.read_string(id, pos);
                const auto found = std::ranges::find(world.doors, at, &::door::pos);
                if (found == world.doors.end()) continue;
                found->dest = destination;
                found->id = id;
            }
        }
    }

    ::blob encode(const ::peer &player)
    {
        ::blob data;
        data.u32(1);
        data.i32(player.gems);
        data.u32(player.skin_color);
        data.u32(player.hair_color);
        for (auto level : player.level) data.i16(static_cast<short>(level));
        data.i16(static_cast<short>(player.clothing.size()));
        for (float id : player.clothing) data.i32(item_id(id));
        if (player.fav.size() > 20) throw std::length_error("Too many saved favorites");
        data.i16(static_cast<short>(player.fav.size()));
        for (short id : player.fav) data.i16(id);
        for (const auto &name : player.recent_worlds) text(data, name);
        for (const auto &name : player.my_worlds) text(data, name);
        finish(data);
        return data;
    }

    void decode(::peer &player, const ::blob &data)
    {
        player.gems = 0;
        player.level = {1, 0};
        player.clothing.fill(0);
        player.fav.clear();
        player.recent_worlds.fill("");
        player.my_worlds.fill("");
        if (data.size() == 0) return;
        int pos{};
        version(data, pos);
        player_header(player, data, pos);
        clothes(player, data, pos);
        favorites(player, data, pos);
        for (auto &name : player.recent_worlds) data.read_string(name, pos);
        for (auto &name : player.my_worlds) data.read_string(name, pos);
        if (static_cast<std::size_t>(pos) != data.size()) throw std::out_of_range("Unexpected saved player data");
    }

    ::blob encode(const ::world &world)
    {
        ::blob data;
        data.u32(1);
        data.u8(world.is_public);
        data.u8(world.lock_state);
        data.u8(world.minimum_entry_level);
        for (int uid : world.access) data.i32(uid);
        data.i32(coordinate(world.weather.x, 100));
        data.i32(coordinate(world.weather.y, 60));
        if (world.doors.size() > 6000) throw std::out_of_range("Too many saved doors");
        data.i16(static_cast<short>(world.doors.size()));
        for (const auto &door : world.doors)
        {
            data.i32(coordinate(door.pos.x, 100));
            data.i32(coordinate(door.pos.y, 60));
            text(data, door.dest);
            text(data, door.id);
        }
        finish(data);
        return data;
    }

    void decode(::world &world, const ::blob &data)
    {
        if (data.size() == 0) return;
        int pos{};
        version(data, pos);
        u_char public_state{};
        data.read_u8(public_state, pos);
        world.is_public = public_state != 0;
        data.read_u8(world.lock_state, pos);
        data.read_u8(world.minimum_entry_level, pos);
        for (int &uid : world.access) data.read_i32(uid, pos);
        world.weather = position(data, pos);
        doors(world, data, pos);
        if (static_cast<std::size_t>(pos) != data.size()) throw std::out_of_range("Unexpected saved world data");
    }
}
