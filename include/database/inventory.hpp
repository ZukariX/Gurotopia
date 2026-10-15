#pragma once

#include <vector>

struct slot;

namespace inventory
{
    bool valid_id(int id);
    int count(const std::vector<::slot> &slots, int id);
    short change(std::vector<::slot> &slots, int capacity, const ::slot &request);
    bool plan(const std::vector<::slot> &slots, int capacity, const std::vector<::slot> &take,
              const std::vector<::slot> &give, std::vector<::slot> &after);
}
