#version 460

#include "functions.glsl"

layout(std140, set=0, binding=0) uniform global_data_t
{
    statistics_t stats;
    camera_t camera;
    scene_t scene;
};

layout(location=0) out vec4 color;

vec3 positions[6] = vec3[6](
    vec3(0, 0, 0),
    vec3(+1, 0, 0),
    vec3(0, 0, 0),
    vec3(0, +1, 0),
    vec3(0, 0, 0),
    vec3(0, 0, +1)
);

vec3 colors[6] = vec3[6](
    vec3(1, 0, 0),
    vec3(1, 0, 0),
    vec3(0, 1, 0),
    vec3(0, 1, 0),
    vec3(0, 0, 1),
    vec3(0, 0, 1)
);

layout(push_constant) uniform push_constants
{
    mat4x4 transform;
    uint force_depth_1;
};

void main()
{
    vec2 screen_ratio = normalize(stats.screen_size);

    transform_t gizmo_transform;
    gizmo_transform.scale = vec3(0.225 * screen_ratio.y, -0.225 * screen_ratio.x, 0.225);
    gizmo_transform.location = vec3(-0.75, 0.75, 0.5);
    gizmo_transform.rotation[0] = -camera.rotation[1];
    gizmo_transform.rotation[1] = -camera.rotation[2];
    gizmo_transform.rotation[2] = -camera.rotation[3];
    gizmo_transform.rotation[3] = camera.rotation[0];

    gl_Position = vec4(world_space_transform(positions[gl_VertexIndex], gizmo_transform), 1.0);
    color = vec4(colors[gl_VertexIndex], 1.0);
}