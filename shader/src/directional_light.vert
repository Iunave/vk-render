#version 460

#extension GL_EXT_scalar_block_layout : require

#include "functions.glsl"

layout(scalar, set=0, binding=0) readonly buffer entity_transforms
{
    packed_transform_t transforms[];
};

layout(std430, set=1, binding=0) readonly buffer directional_light
{
    directional_light_data_t light;
};

layout(location=0) in vec3 position;

void main()
{
    transform_t transform = unpack_transform(transforms[gl_BaseInstance]);
    vec3 world_pos = world_space_transform(position, transform);
    gl_Position = light.projection_view * vec4(world_pos, 1.0);
}