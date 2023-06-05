#ifndef CHEEMSIT_GUI_IMGUI_WINDOWS_HPP
#define CHEEMSIT_GUI_IMGUI_WINDOWS_HPP

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace ui
{
    using pfn = void(*)(bool*);
    inline std::string log_string{};

    void iterate_windows();
    bool set_open(pfn, bool open);
    void set_all(bool open);

    void display_stats(bool* popen);
    void display_options(bool* popen);
    void display_camera(bool* popen);
    void display_world(bool* popen);
    void display_console(bool* popen);
}

#endif //CHEEMSIT_GUI_IMGUI_WINDOWS_HPP
