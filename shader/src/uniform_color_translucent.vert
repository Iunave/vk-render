#version 460

#include "functions.glsl"

layout(std140, set=0, binding=0) uniform global_data_t
{
    statistics_t stats;
    camera_t camera;
    scene_t scene;
};

layout(std430, set=1, binding=2) readonly buffer pointlights_buffer
{
    uint size;
    uint pad0[3];
    pointlight_t lights[];
} pointlights;

layout(location=0) in vec3 position;
layout(location=0) out vec3 color;

void main()
{
    vec3 light_pos = pointlights.lights[gl_BaseInstance].location;
    vec3 world_pos = position + light_pos;

    //vec3 hdr_color = pointlights.lights[gl_BaseInstance].color * pointlights.lights[gl_BaseInstance].power;
    color = pointlights.lights[gl_BaseInstance].color;

    gl_Position = camera.projection_view * vec4(world_pos, 1.0);
}