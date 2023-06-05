#version 460

#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

#include "functions.glsl"

const float specular_strength = 0.5;
const float shininess = 32;

void calculate_directional_light(directional_light_t light, vec3 frag_pos, vec3 frag_norm, vec3 frag_world_norm, vec3 view_dir, out vec3 diffuse, out vec3 specular)
{
    float light_hit = max(0.0, dot(frag_norm, vec3(0, 0, -1)));
    float recieved_light = light_hit * light.strength;
    diffuse = light.color * recieved_light;

    vec3 halfway_dir = slerp(-light.direction, view_dir, 0.5);
    float reflected_hit = dot(frag_world_norm, halfway_dir);
    reflected_hit = pow(max(reflected_hit, 0.0), shininess);

    float reflected_light = reflected_hit * specular_strength * light.strength;
    specular = light.color * reflected_light / 100.0; //todo specular is way too bright.. why??
}

void recieve_pointlight(pointlight_t light, vec3 frag_pos, vec3 frag_normal, vec3 view_dir, out vec3 diffuse, out vec3 specular)
{
    vec3 light_dir = normalize(light.location - frag_pos);
    float light_hit = dot(frag_normal, light_dir);
    light_hit = max(light_hit, 0.0);

    float distance_sqrd = distance(light.location, frag_pos);
    distance_sqrd *= distance_sqrd;

    float recieved_light = (light_hit * light.power) / 10000.0; ///distane_sqrt
    diffuse = light.color * recieved_light;

    vec3 halfway_dir = slerp(light_dir, view_dir, 0.5);
    float reflected_hit = dot(frag_normal, halfway_dir);
    reflected_hit = pow(max(reflected_hit, 0.0), shininess);

    float reflected_light = (reflected_hit * specular_strength * light.power) / 10000.0;///distane_sqrt
    specular = light.color * reflected_light;
}

layout(std140, set=0, binding=0) uniform global_data_t
{
    statistics_t stats;
    camera_t camera;
    scene_t scene;
};

layout(std430, set=1, binding=1) readonly buffer directional_light_buffer
{
    uint size;
    uint pad0[3];
    directional_light_data_t data[];
} directional_lights;

layout(std430, set=1, binding=2) readonly buffer pointlights_buffer
{
    uint size;
    uint pad0[3];
    pointlight_t lights[];
} pointlights;

layout(set=2, binding=0) uniform sampler cube_shadow_sampler;
layout(set=2, binding=1) uniform textureCube cube_shadows[];
layout(set=3, binding=0) uniform sampler2DShadow directional_shadows[];
layout(set=4, binding=0) uniform sampler2D color_texture;

in data_t
{
    layout(location=0) vec3 world_pos;
    layout(location=1) vec3 world_normal;
    layout(location=2) vec2 uv;
    layout(location=3) vec3 view_dir;
} indata;

layout(location=0) out vec4 frag_color;

void main()
{
    vec4 tex_color = texture(color_texture, indata.uv);
    if(tex_color.a == 0.0)
    {
        discard;
    }

    vec3 ambient = scene.ambiance_color * scene.ambiance_strength;
    vec3 diffuse = vec3(0,0,0);
    vec3 specular = vec3(0,0,0);

    for(uint index = 0; index < directional_lights.size; index += 1)
    {
        directional_light_data_t light_data = directional_lights.data[index];

        vec4 frag_pos = light_data.projection_view * vec4(indata.world_pos, 1.0);
        vec4 frag_norm = light_data.projection_view * vec4(indata.world_normal, 0.0);

        float shadow = in_plane_shadow(directional_shadows[index], frag_pos.xyz, frag_norm.xyz);
        float light = 1.0 - shadow;

        if(light <= 0.0)
        {
            continue;
        }

        vec3 directional_diffuse;
        vec3 directional_specular;
        calculate_directional_light(light_data.light, frag_pos.xyz, frag_norm.xyz, indata.world_normal, indata.view_dir, directional_diffuse, directional_specular);

        diffuse += light * directional_diffuse;
        specular += light * directional_specular;
    }

    for(uint index = 0; index < pointlights.size; index += 1)
    {
        pointlight_t pointlight = pointlights.lights[index];

        float shadow = in_cube_shadow(cube_shadows[index], cube_shadow_sampler, camera.location.xyz, pointlight.location, pointlight.power, indata.world_pos, indata.world_normal);
        float light = 1.0 - shadow;

        if(light <= 0.0)
        {
            continue;
        }

        vec3 pointlight_diffuse;
        vec3 pointlight_specular;
        recieve_pointlight(pointlight, indata.world_pos, indata.world_normal, indata.view_dir, pointlight_diffuse, pointlight_specular);

        diffuse += light * pointlight_diffuse;
        specular += light * pointlight_specular;
    }

    vec3 color = tex_color.rgb * (ambient + diffuse + specular);
    frag_color = vec4(color, tex_color.a);
}