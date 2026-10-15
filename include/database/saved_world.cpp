#include "pch.hpp"
#include "saved_state.hpp"
#include <set>

namespace saved_state
{
    namespace
    {
        bool valid_item(int id) { return id >= 0 && id <= 32767 && static_cast<std::size_t>(id) < items.size(); }

        void extra(::world &world, const ::pos &at, char kind, const ::blob &data, int &offset)
        {
            if (kind == '\x01')
            {
                auto &door = world.doors.emplace_back("", "", "", at);
                data.read_string(door.label, offset);
                u_char terminator{};
                data.read_u8(terminator, offset);
                if (terminator != 0) throw std::runtime_error("Invalid saved door label");
            }
            else if (kind == '\x02')
            {
                auto &sign = world.signs.emplace_back("", at);
                data.read_string(sign.label, offset);
                data.read_u32(sign.idk, offset);
            }
            else if (kind == '\x04')
            {
                auto &tree = world.trees.emplace_back(0, 0, at);
                data.read_u32(tree.tick, offset);
                data.read_u8(tree.fruit, offset);
            }
        }

        template<typename T>
        void append(::blob &data, const std::vector<T> &entries, const ::pos &at)
        {
            const auto found = std::ranges::find(entries, at, &T::pos);
            if (found == entries.end()) throw std::runtime_error("Missing saved tile data");
            data.push_back(found->to_blob());
        }

        void extra(const ::world &world, const ::pos &at, char kind, ::blob &data)
        {
            if (kind == '\x01') append(data, world.doors, at);
            else if (kind == '\x02') append(data, world.signs, at);
            else if (kind == '\x04') append(data, world.trees, at);
        }

        void validate(const ::slot &slot)
        {
            if (!valid_item(slot.id) || slot.id == 0 || slot.count < 0 || slot.count > 200)
                throw std::out_of_range("Invalid saved inventory item");
        }

        void validate(const ::object &object)
        {
            if (!valid_item(object.id) || object.id == 0 || object.count == 0 || object.count > 200)
                throw std::out_of_range("Invalid saved drop");
            if (!std::isfinite(object.pos.x) || !std::isfinite(object.pos.y))
                throw std::out_of_range("Invalid saved drop position");
        }
    }

    void inventory(::peer &player, std::vector<u_char> bytes)
    {
        ::blob data;
        data.data() = std::move(bytes);
        int offset{}, capacity{};
        short size{};
        data.read_i32(capacity, offset);
        data.read_i16(size, offset);
        if (capacity < 16 || capacity > 396) throw std::out_of_range("Invalid saved backpack size");
        if (size < 0) throw std::out_of_range("Invalid saved inventory size");
        std::vector<::slot> slots;
        std::set<short> seen;
        for (int i = 0; i < size; ++i)
        {
            ::slot slot;
            data.read_i16(slot.id, offset);
            data.read_i16(slot.count, offset);
            validate(slot);
            if (!seen.insert(slot.id).second) throw std::out_of_range("Duplicate saved inventory item");
            slots.push_back(slot);
        }
        if (static_cast<std::size_t>(offset) != data.size()) throw std::out_of_range("Unexpected saved inventory data");
        player.slot_size = capacity;
        player.slots = std::move(slots);
    }

    void blocks(::world &world, const ::blob &data)
    {
        world.doors.clear();
        world.signs.clear();
        world.trees.clear();
        world.blocks.resize(6000);
        int offset{};
        for (int i = 0; auto &block : world.blocks)
        {
            data.read_i16(block.fg, offset);
            data.read_i16(block.bg, offset);
            for (auto &state : block.state) data.read_u8(state, offset);
            if (!valid_item(block.fg) || !valid_item(block.bg)) throw std::out_of_range("Invalid saved tile");
            extra(world, ::pos{i % 100, i / 100}, get_type(id_to_item(block.fg)), data, offset);
            ++i;
        }
        if (static_cast<std::size_t>(offset) != data.size()) throw std::out_of_range("Unexpected saved tile data");
    }

    void objects(::world &world, const ::blob &data)
    {
        if (data.size() == 0) { world.objects.clear(); world.last_object_uid = 0; return; }
        if (data.size() < 4 || (data.size() - 4) % 16 != 0) throw std::out_of_range("Invalid saved drop data");
        int offset{};
        data.read_u32(world.last_object_uid, offset);
        std::vector<::object> objects((data.size() - 4) / 16);
        std::set<u_int> seen;
        for (auto &object : objects)
        {
            short id{}, count{};
            data.read_i16(id, offset);
            data.read_f32(object.pos.x, offset);
            data.read_f32(object.pos.y, offset);
            data.read_i16(count, offset);
            data.read_u32(object.uid, offset);
            object.id = id;
            object.count = count;
            validate(object);
            if (object.uid == 0 || object.uid > world.last_object_uid || !seen.insert(object.uid).second)
                throw std::out_of_range("Invalid saved drop identity");
        }
        world.objects = std::move(objects);
    }

    ::blob blocks(const ::world &world)
    {
        if (world.blocks.size() != 6000) throw std::out_of_range("Invalid world size");
        ::blob data;
        for (int i = 0; const auto &block : world.blocks)
        {
            if (!valid_item(block.fg) || !valid_item(block.bg)) throw std::out_of_range("Invalid tile");
            data.push_back(block.to_blob());
            extra(world, ::pos{i % 100, i / 100}, get_type(id_to_item(block.fg)), data);
            ++i;
        }
        if (data.size() > 65535) throw std::length_error("Saved map exceeds blob limit");
        return data;
    }

    ::blob objects(const ::world &world)
    {
        ::blob data;
        data.u32(world.last_object_uid);
        for (const auto &object : world.objects)
        {
            validate(object);
            data.push_back(object.to_blob());
        }
        if (data.size() > 65535) throw std::length_error("Saved drops exceed blob limit");
        return data;
    }
}
