#ifndef CHEEMSIT_GUI_CAMERA
#define CHEEMSIT_GUI_CAMERA

#include "vector_types.hpp"
#include <memory>

class camera_t
{
public:
    camera_t();

    glm::vec3 forward_vector() const {return rotation * axis::forward;}
    glm::vec3 backard_vector() const {return rotation * axis::backward;}
    glm::vec3 left_vector() const {return rotation * axis::left;}
    glm::vec3 right_vector() const {return rotation * axis::right;}
    glm::vec3 up_vector() const {return rotation * axis::up;}
    glm::vec3 down_vector() const {return rotation * axis::down;}
    void rotate(float angle, glm::vec3 axis);

    void tick(double delta_time);

    bool can_see(glm::vec3 point) const;

    glm::mat4x4 view_matrix() const;
    glm::mat4x4 projection_matrix() const;

    glm::vec3 location;
    glm::quat rotation;

    float FOVy;
    float z_near;
    float z_far;
    float move_speed;
    float look_speed;
    float roll_speed;
    bool lock_roll;
};

#endif //CHEEMSIT_GUI_CAMERA
