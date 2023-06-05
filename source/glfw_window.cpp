#include "glfw_window.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <fmt/format.h>
#include "imgui_windows.hpp"
#include "log.hpp"

static void glfw_error(int error = 0, const char* description = nullptr)
{
    LogWindow("error: {} - {}", error, description ? description : "?");
    abort();
}

static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos)
{
}

static void toggle_cursor_capture(GLFWwindow* window)
{
    static bool toggle = false;
    toggle = !toggle;

    glfwSetInputMode(window, GLFW_CURSOR, toggle ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

static void toggle_UI()
{
    static bool toggle = true;
    toggle = !toggle;

    ui::set_all(toggle);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if(action == GLFW_PRESS)
    {
        if(mods & GLFW_MOD_CONTROL)
        {
            switch(key)
            {
                default:
                    break;
            }
        }
        else
        {
            switch(key)
            {
                case GLFW_KEY_ESCAPE:
                    toggle_cursor_capture(window);
                    break;
                case GLFW_KEY_I:
                    toggle_UI();
                break;
                default:
                    break;
            }
        }
    }
    else if(action == GLFW_RELEASE)
    {
        switch(key)
        {
            default:
                break;
        }
    }
}

GLFWwindow* create_window()
{
    LogWindow("creating window");

    glfwSetErrorCallback(glfw_error);

    //glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11); does not seem to be support for xcb with glfw
    //glfwInitHint(GLFW_X11_XCB_VULKAN_SURFACE, GLFW_TRUE);

    if(!glfwInit())
    {
        glfw_error(0, "glfwInit");
    }

    if(!glfwVulkanSupported())
    {
        glfw_error(0, "!glfwVulkanSupported");
    }

    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(1000, 1000, "vk-render", nullptr, nullptr);
    if(!window)
    {
        glfw_error(0, "glfwCreateWindow");
    }

    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);

    return window;
}

void close_window(GLFWwindow *window)
{
    LogWindow("closing window");

    glfwDestroyWindow(window);
    glfwTerminate();
}
