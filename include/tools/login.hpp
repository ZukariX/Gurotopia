#pragma once

namespace login
{
    struct ticket { int token{}; std::string uuid; };
    bool issue(const ::peer &player, ticket &result);
    bool consume(::peer &player, const ::hPipe &packet);
    bool allowed(const ::peer &player, std::string_view action);
    void refuse(ENetEvent &event);
}
