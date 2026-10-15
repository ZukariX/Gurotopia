#pragma once

#include <string_view>

namespace https
{
    enum class request_state { incomplete, complete, invalid };
    request_state check_request(std::string_view bytes);
}
