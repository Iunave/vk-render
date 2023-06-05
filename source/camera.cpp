#include "camera.hpp"
#include "math.hpp"
#include "glfw_window.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

using std::numbers::pi;

camera_t::camera_t()
{
    location = {0,0,0};
    rotation = {1, 0, 0, 0};
    lock_roll = true;
    FOVy = pi / 4.0;
    z_near = 0.1;
    z_far = 10000.0;
    move_speed = 50.0;
    look_speed = 0.001;
    roll_speed = 1.0;
}

void camera_t::rotate(float angle, glm::vec3 axis)
{
    rotation = glm::normalize(glm::angleAxis(angle, axis) * rotation);
}

void camera_t::tick(double delta_time)
{
    double xpos; double ypos;
    glfwGetCursorPos(gWindow, &xpos, &ypos);

    static float last_xpos = xpos;
    static float last_ypos = ypos;

    if(glfwGetInputMode(gWindow, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
    {
        float move_update_speed = move_speed * delta_time;
        float roll_update_speed = roll_speed * delta_time;

        float delta_xpos = xpos - last_xpos;
        float delta_ypos = ypos - last_ypos;

        float yaw_angle = delta_xpos * look_speed;
        float pitch_angle = delta_ypos * look_speed;

        rotate(yaw_angle, lock_roll ? axis::up : up_vector());
        rotate(pitch_angle, right_vector());

        if(!lock_roll && glfwGetKey(gWindow, GLFW_KEY_E) == GLFW_PRESS)
        {
            rotate(-roll_update_speed, forward_vector());
        }
        if(!lock_roll && glfwGetKey(gWindow, GLFW_KEY_Q) == GLFW_PRESS)
        {
            rotate(roll_update_speed, forward_vector());
        }
        if(glfwGetKey(gWindow, GLFW_KEY_W) == GLFW_PRESS)
        {
            location += forward_vector() * move_update_speed;
        }
        if(glfwGetKey(gWindow, GLFW_KEY_S) == GLFW_PRESS)
        {
            location += backard_vector() * move_update_speed;
        }
        if(glfwGetKey(gWindow, GLFW_KEY_D) == GLFW_PRESS)
        {
            location += right_vector() * move_update_speed;
        }
        if(glfwGetKey(gWindow, GLFW_KEY_A) == GLFW_PRESS)
        {
            location += left_vector() * move_update_speed;
        }
        if(glfwGetKey(gWindow, GLFW_KEY_SPACE) == GLFW_PRESS)
        {
            location += up_vector() * move_update_speed;
        }
        if(glfwGetKey(gWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        {
            location += down_vector() * move_update_speed;
        }
    }

    last_xpos = xpos;
    last_ypos = ypos;
}

glm::mat4x4 camera_t::view_matrix() const
{
    glm::vec3 Cx = rotation * axis::X;
    glm::vec3 Cy = rotation * axis::Y;
    glm::vec3 Cz = rotation * axis::Z;

    glm::mat4x4 view;
    view[0] = {Cx, 0};
    view[1] = {Cy, 0};
    view[2] = {Cz, 0};
    view[3] = {location, 1};
    view = glm::inverse(view);

    glm::mat4x4 vulkan; //goes from world space (+X = right, +Y = up +Z = forward) to vulkan space (+X = right -Y = up, +Z = forward)
    vulkan[0] = {1, 0, 0, 0};
    vulkan[1] = {0, -1, 0, 0};
    vulkan[2] = {0, 0, 1, 0};
    vulkan[3] = {0, 0, 0, 1};
    vulkan = glm::inverse(vulkan); //unneccesairy step but lol

    return vulkan * view;
}

glm::mat4x4 camera_t::projection_matrix() const
{
    int32_t width; int32_t height;
    glfwGetFramebufferSize(gWindow, &width, &height);

    return math::perspective(float(width) / float(height), FOVy, z_near, z_far);
}

bool camera_t::can_see(glm::vec3 point) const
{
    glm::vec3 look_direction = forward_vector();
    glm::vec3 to_point_direction = glm::normalize(point - location);
    float angle = std::acos(glm::dot(look_direction, to_point_direction));
    return angle <= FOVy;
}
