#ifndef CHEEMSIT_GUI_VK_NAME_HPP
#define CHEEMSIT_GUI_VK_NAME_HPP

#include "vk-render.hpp"
#include <memory>
#include <set>
#include <cassert>
#include <string>
#include <string_view>

inline constexpr size_t name_len_max = 64;

class shared_name_t
{
public:
    template<size_t N>
    constexpr shared_name_t(const char(&data)[N] = "") requires(N < name_len_max)
    {
        memset(string, 0, name_len_max);
        memcpy(string, data, N);
    }

    shared_name_t(std::string_view str)
    {
        assert(str.length() < name_len_max);

        memset(string, 0, name_len_max);
        memcpy(string, str.data(), str.length());
    }

    char string[name_len_max];
};

inline auto operator<=>(const shared_name_t& lhs, const shared_name_t& rhs)
{
    return strcmp(lhs.string, rhs.string) <=> 0;
}

struct shared_name_hasher
{
    size_t operator()(const shared_name_t& name);
};

class name_t
{
public:
    static inline std::set<shared_name_t> shared_names;
    static const name_t empty_name;
    static const name_t null_name;

    name_t()
        : name_t(empty_name)
    {
    }

    name_t(name_t&& other)
    {
        reference = other.reference;
        other = empty_name;
    }

    name_t(const name_t& other)
    {
        reference = other.reference;
    }

    name_t& operator=(name_t&& other)
    {
        reference = other.reference;
        other = empty_name;
        return *this;
    }

    name_t& operator=(const name_t& other)
    {
        reference = other.reference;
        return *this;
    }

    template<size_t N>
    name_t(const char(&data)[N])
    {
        reference = shared_names.insert(data).first;
    }

    name_t(std::string_view data)
    {
        reference = shared_names.insert(data).first;
    }

    name_t(const std::string& data)
        : name_t(std::string_view(data))
    {
    }

    template<size_t N>
    name_t& operator=(const char(&data)[N]) requires(N < 64)
    {
        reference = shared_names.insert(data).first;
        return *this;
    }

    name_t& operator=(std::string_view data)
    {
        reference = shared_names.insert(data).first;
        return *this;
    }

    inline friend bool operator==(const name_t& lhs, const name_t& rhs)
    {
        return lhs.reference == rhs.reference;
    }

    inline friend bool operator!=(const name_t& lhs, const name_t& rhs)
    {
        return lhs.reference != rhs.reference;
    }

    bool empty() const
    {
        return reference == empty_name.reference;
    }

    operator std::string_view() const
    {
        return str();
    }

    std::string_view str() const
    {
        return reference->string;
    }

    const char* data() const
    {
        return reference->string;
    }

    decltype(shared_names)::const_iterator reference;
};

#endif //CHEEMSIT_GUI_VK_NAME_HPP
