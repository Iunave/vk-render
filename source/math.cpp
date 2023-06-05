#include "math.hpp"

// Returns ±1
static glm::vec2 sign_not_zero(glm::vec2 v)
{
    return glm::vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
}

glm::vec2 math::oct_encode(glm::vec3 v)
{
    //Project the sphere onto the octahedron, and then onto the xy plane
    glm::vec2 p = glm::vec2(v.x, v.y) * (1.0f / (abs(v.x) + abs(v.y) + abs(v.z)));
    // Reflect the folds of the lower hemisphere over the diagonals
    return (v.z <= 0.f) ? ((1.0f - glm::vec2(abs(p.y), abs(p.x))) * sign_not_zero(p)) : p;
}

glm::vec3 math::oct_decode(glm::vec2 e)
{
    glm::vec3 v = glm::vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
    if(v.z < 0.f)
    {
        glm::vec2 vxy = (1.0f - glm::vec2(abs(v.y), abs(v.x))) * sign_not_zero(glm::vec2(v.x, v.y));
        v.x = vxy.x;
        v.y = vxy.y;
    }
    return normalize(v);
}

void math::oct_error_test()
{
    uint64_t tests = 100;
    double total_error = 0.0;
    for(uint64_t index = 0; index < tests; ++index)
    {
        glm::vec3 randvec = math::rand_axis();
        glm::vec2 oct_encoded = math::oct_encode(randvec);
        glm::vec3 oct_decoded = math::oct_decode(oct_encoded);
        glm::vec3 error;
        error.x = abs(randvec.x - oct_decoded.x);
        error.y = abs(randvec.y - oct_decoded.y);
        error.z = abs(randvec.z - oct_decoded.z);
        double error_sum = error.x + error.y + error.z;
        total_error += error_sum;

        printf("encoded:  %f | %f\n", oct_encoded.x, oct_encoded.y);
        printf("original: %f | %f | %f\n", randvec.x, randvec.y, randvec.z);
        printf("decoded:  %f | %f | %f\n", oct_decoded.x, oct_decoded.y, oct_decoded.z);
        printf("error:    %f | %f | %f\n\n", error.x, error.y, error.z);
    }

    double error_percentage = total_error / double(tests);
    printf("error percentage %f\n", error_percentage);
}

glm::mat4x4 math::perspective(float aspect, float fov, float near, float far)
{
    glm::mat4x4 result;
    result[0] = {(1.0 / aspect) / std::tan(fov / 2.0), 0, 0, 0};
    result[1] = {0, 1.0 / std::tan(fov / 2.0), 0, 0};
    result[2] = {0, 0, near / (near - far), 1};
    result[3] = {0, 0, -((far * near) / (near - far)), 0};

    return result;
}

glm::mat4x4 math::view(glm::quat orientation, glm::vec3 location)
{
    glm::vec3 Cx = orientation * axis::X;
    glm::vec3 Cy = orientation * axis::Y;
    glm::vec3 Cz = orientation * axis::Z;

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

glm::mat4x4 math::view(glm::vec3 Cx, glm::vec3 Cy, glm::vec3 Cz, glm::vec3 location)
{
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

glm::mat4x4 math::view(glm::vec3 forward, glm::vec3 up, glm::vec3 location)
{
    return view(glm::quatLookAt(forward, up), location);
};
