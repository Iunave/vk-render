#ifndef CHEEMSIT_GUI_VK_LOG_HPP
#define CHEEMSIT_GUI_VK_LOG_HPP

#include <fmt/core.h>
#include <fmt/color.h>
#include <string_view>
#include <string>
#include <cstdint>
#include "vk-render.hpp"
#include "imgui_windows.hpp"

#ifndef NDEBUG

class FLogCategory
{
public:
    template<size_t NumMsgChars, typename... FmtArgs>
    void operator()(const char(&Message)[NumMsgChars], FmtArgs&&... FormatArgs) const
    {
        std::string msg = fmt::format(fmt::runtime("{} {}\n"), prepend, Message);
        msg = fmt::format(fmt::runtime(msg), std::forward<FmtArgs>(FormatArgs)...);

        mx.lock();
            fmt::print(fmt::fg(color), msg);
            ui::log_string.append(msg);
        mx.unlock();
    }

    const std::string_view prepend;
    const fmt::color color;
    static inline std::mutex mx{};
};

inline constexpr FLogCategory LogTemp{"temp:", fmt::color::dark_gray};
inline constexpr FLogCategory LogWorld{"world:", fmt::color::brown};
inline constexpr FLogCategory LogVulkan{"vulkan:", fmt::color::orange_red};
inline constexpr FLogCategory LogFileLoader{"loader:", fmt::color::light_steel_blue};
inline constexpr FLogCategory LogWindow{"window:", fmt::color::gold};

#else

#define LogTemp(...) void()
#define LogWorld(...) void()
#define LogVulkan(...) void()
#define LogMemory(...) void()
#define LogFileLoader(...) void()
#define LogWindow(...) void()

#endif //DEBUG

#endif //CHEEMSIT_GUI_VK_LOG_HPP
