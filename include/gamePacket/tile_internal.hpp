#pragma once

namespace tiles
{
    struct change
    {
        ENetEvent &event;
        ::gamePacket packet;
        ::peer &player;
        ::world &world;
        ::block &tile;
        const ::item &item;
    };

    bool punch(change &action);
    void consume(change &action);
    void wrench(change &action);
    void place(change &action);
}
