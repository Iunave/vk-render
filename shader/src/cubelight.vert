#version 460

#extension GL_EXT_scalar_block_layout : enable

#include "functions.glsl"

layout(scalar, set=1, binding=0) readonly buffer entity_transforms
{
    packed_transform_t entities[];
};

layout(location=0) in vec3 position;

void main()
{
    transform_t transform = unpack_transform(entities[gl_BaseInstance]);
    vec3 world_pos = world_space_transform(position, transform);

    gl_Position = vec4(world_pos, 1.0);
}