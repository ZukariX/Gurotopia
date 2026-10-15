#pragma once

namespace saved_state
{
    void inventory(::peer &player, std::vector<u_char> bytes);
    void blocks(::world &world, const ::blob &data);
    void objects(::world &world, const ::blob &data);
    ::blob blocks(const ::world &world);
    ::blob objects(const ::world &world);
    ::blob encode(const ::peer &player);
    ::blob encode(const ::world &world);
    void decode(::peer &player, const ::blob &data);
    void decode(::world &world, const ::blob &data);
}
