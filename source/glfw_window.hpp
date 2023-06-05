#ifndef CHEEMSIT_GUI_VK_GLFW_WINDOW_HPP
#define CHEEMSIT_GUI_VK_GLFW_WINDOW_HPP

struct GLFWwindow;

GLFWwindow* create_window();
void close_window(GLFWwindow* window);

inline GLFWwindow* gWindow = nullptr;

#endif //CHEEMSIT_GUI_VK_GLFW_WINDOW_HPP
