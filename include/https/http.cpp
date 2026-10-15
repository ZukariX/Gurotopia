#include "http.hpp"
#include <charconv>
#include <cctype>
#include <algorithm>

namespace https
{
    namespace
    {
        bool equal_name(std::string_view first, std::string_view second)
        {
            if (first.size() != second.size()) return false;
            auto next = second.begin();
            return std::ranges::all_of(first, [&next](unsigned char c) {
                return std::tolower(c) == std::tolower(static_cast<unsigned char>(*next++));
            });
        }

        std::string_view trim(std::string_view value)
        {
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
            return value;
        }

        bool header(std::string_view line, bool &seen, std::size_t &length)
        {
            const auto colon = line.find(':');
            if (colon == std::string_view::npos) return false;
            const auto name = line.substr(0, colon);
            if (equal_name(name, "Transfer-Encoding")) return false;
            if (!equal_name(name, "Content-Length")) return true;
            if (seen) return false;
            seen = true;
            const auto value = trim(line.substr(colon + 1));
            if (value.empty()) return false;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
            return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
        }

        bool body_length(std::string_view headers, std::size_t &length)
        {
            bool seen = false;
            while (!headers.empty())
            {
                const auto end = headers.find("\r\n");
                if (!header(headers.substr(0, end), seen, length)) return false;
                if (end == std::string_view::npos) break;
                headers.remove_prefix(end + 2);
            }
            return length <= 8192;
        }
    }

    request_state check_request(std::string_view bytes)
    {
        if (bytes.size() > 16384) return request_state::invalid;
        const auto end = bytes.find("\r\n\r\n");
        if (end == std::string_view::npos) return request_state::incomplete;
        const auto first = bytes.find("\r\n");
        if (first == 0 || first > end) return request_state::invalid;
        std::size_t length{};
        if (!body_length(bytes.substr(first + 2, first == end ? 0 : end - first - 2), length)) return request_state::invalid;
        return bytes.size() >= end + 4 + length ? request_state::complete : request_state::incomplete;
    }
}
