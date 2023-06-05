#version 460

#include "functions.glsl"

layout(std430, set=0, binding=1) readonly buffer pointlight_buffer
{
    pointlight_t pointlight;
};

layout(location=0) in vec4 world_pos;

void main()
{
    float light_distance = distance(world_pos.xyz, pointlight.location);
    float depth = light_distance / pointlight.power;

    gl_FragDepth = depth;
}