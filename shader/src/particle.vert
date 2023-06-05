#version 460

#include "functions.glsl"

layout(std140, set=0, binding=0) uniform global_data_t
{
    statistics_t stats;
    camera_t camera;
    scene_t scene;
};

layout(location=0) in vec3 vtx_position;
layout(location=1) in vec2 vtx_normal;
layout(location=2) in vec2 vtx_uv;
layout(location=3) in vec3 inst_position;
layout(location=4) in vec3 inst_scale;

layout(location=0) out vec2 uv;

void main()
{
    //vec3 world_pos = vtx_position * inst_scale + inst_position;

    mat4x4 inv_view = inverse(camera.view);
    vec3 camera_up = (inv_view * vec4(0, -1, 0, 0)).xyz;
    vec3 camera_right = (inv_view * vec4(-1, 0, 0, 0)).xyz;
    vec3 camera_forward = (inv_view * vec4(0, 0, -1, 0)).xyz;

    vec3 vpos = vtx_position * inst_scale;
    vec3 world_pos = inst_position + camera_right * vpos.x
                                   + camera_up * vpos.y
                                   + camera_forward * vpos.z;

    uv = vtx_uv;
    gl_Position = camera.projection_view * vec4(world_pos, 1.0);
}