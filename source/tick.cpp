#include "tick.hpp"
#include <vector>
#include <algorithm>
#include <bit>
#include <memory>
#include <cstring>
#include <fmt/format.h>

namespace tick
{
    std::vector<std::pair<pfn, void*>> tick_vector{};
}

void tick::dispatch(double delta_time)
{
    for(auto element : tick_vector)
    {
        element.first(element.second, delta_time);
    }
}

static bool pfn_less(std::pair<tick::pfn, void*> lhs, std::pair<tick::pfn, void*> rhs)
{
    return std::bit_cast<uint64_t>(lhs.first) < std::bit_cast<uint64_t>(rhs.first);
}

static bool data_less(std::pair<tick::pfn, void*> lhs, std::pair<tick::pfn, void*> rhs)
{
    return lhs.second < rhs.second;
}

void tick::add(pfn fn, void* data)
{
    std::pair<tick::pfn, void*> pair{fn, data};
    tick_vector.emplace_back(pair);
    return;

    auto fn_lower = std::lower_bound(tick_vector.begin(), tick_vector.end(), pair, pfn_less);
    auto fn_upper = std::upper_bound(fn_lower, tick_vector.end(), pair, pfn_less);
    auto data_upper = std::upper_bound(fn_lower, fn_upper, pair, data_less);

    if(data_upper == tick_vector.end())
    {
        tick_vector.emplace_back(pair);
    }
    else
    {
        //tick_vector.resize(tick_vector.size() + 1);

        //std::memmove(std::to_address(data_upper + 1), std::to_address(data_upper), std::distance(data_upper, tick_vector.end() - 1) * sizeof(pair));

        //tick_vector.insert(data_upper - 1, pair);
    }
}

bool tick::remove(pfn fn, void* data) //todo sort and binary search here..
{
    for(auto it = tick_vector.begin(); it != tick_vector.end(); ++it)
    {
        if(it->first == fn && it->second == data)
        {
            tick_vector.erase(it);
            return true;
        }
    }

    return false;
}
