#ifndef CHEEMSIT_GUI_VK_VK_RENDER_HPP
#define CHEEMSIT_GUI_VK_VK_RENDER_HPP

#include <cstdint>
#include <string>
#include <cerrno>
#include <cstring>
#include <vector>

#define UNLIKELY(xpr) (__builtin_expect(!!(xpr), 0))
#define LIKELY(xpr) (__builtin_expect(!!(xpr), 1))

#define TYPE_ALLOCA(type, count) static_cast<type*>(alloca(sizeof(type) * count))

namespace tf
{
    class Executor;
}

inline tf::Executor* tf_executor = nullptr;

inline constexpr uint64_t rotleft(uint64_t val, uint8_t by)
{
    return __builtin_rotateleft64(val, by);
}

inline constexpr uint64_t pad_size2alignment(uint64_t size, uint64_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

class syscheck_t
{
public:
    syscheck_t& operator=(__syscall_slong_t in_val);

    operator __syscall_slong_t() const {return val;}
    __syscall_slong_t val = 0;
};
inline constinit thread_local syscheck_t syscheck;

__attribute__((noreturn)) void syscall_error(const char* str);

void prepare_logfile();
void close_logfile();

std::vector<uint8_t> read_file_binary(std::string_view filepath);
std::vector<uint8_t> read_file_binary(std::string_view dir, std::string_view file);
std::vector<uint8_t> read_file_binary(int dir, std::string_view file);

extern "C" uint32_t count_mipmap(uint32_t width, uint32_t height) __attribute__((pure, leaf));

#endif //CHEEMSIT_GUI_VK_VK_RENDER_HPP
