#include "name.hpp"
#include <string_view>

size_t shared_name_hasher::operator()(const shared_name_t& name)
{
    size_t hash = 0;
    uint64_t index = 0;

    for(;name.string[index] != 0; ++index)
    {
        hash ^= size_t(name.string[index]) << index;
    }

    return hash + index;
}

const name_t name_t::empty_name{""};
const name_t name_t::null_name{"null"};
