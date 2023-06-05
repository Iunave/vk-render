#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable

#include "functions.glsl"

layout(std140, set=0, binding=0) uniform global_data_t
{
    statistics_t stats;
    camera_t camera;
    scene_t scene;
};

layout(scalar, set=1, binding=0) readonly buffer entity_transforms
{
    packed_transform_t transforms[];
};

layout(location=0) in vec3 position;
layout(location=1) in vec2 oct_normal; //oct encoded
layout(location=2) in vec2 uv;

out data_t
{
    layout(location=0) vec3 world_pos; //world space
    layout(location=1) vec3 world_normal; //world space
    layout(location=2) vec2 uv;
    layout(location=3) vec3 view_dir;
} outdata;

void main()
{
    transform_t transform = unpack_transform(transforms[gl_BaseInstance]);
    vec3 world_pos = world_space_transform(position, transform);
    gl_Position = camera.projection_view * vec4(world_pos, 1.0);

    vec3 normal = oct_decode(oct_normal);

    outdata.world_pos = world_pos;
    outdata.uv = uv;
    outdata.world_normal = rotate_vector(transform.rotation, normal);
    outdata.view_dir = normalize(camera.location.xyz - world_pos);
}