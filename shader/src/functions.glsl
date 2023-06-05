struct transform_t
{
    vec4 rotation; //quat
    vec3 location;
    vec3 scale;
};

struct packed_transform_t
{
    uint rotation_wx;
    uint rotation_yz;
    vec3 location;
    vec3 scale;
};

struct pointlight_t
{
    vec3 location;
    float pad0;
    vec3 color;
    float power;
};

struct directional_light_t
{
    vec3 direction;
    float pad0;
    vec3 color;
    float strength;
};

struct directional_light_data_t
{
    directional_light_t light;
    mat4x4 projection_view;
};

struct statistics_t
{
    int elapsed_seconds;
    int elapsed_nanoseconds;
    uint frame;
    float deltatime;
    uvec2 screen_size;
    float elapsed_time;
    float pad0;
};

struct camera_t
{
    vec3 location;
    float pad0;
    vec4 rotation;
    mat4x4 view;
    mat4x4 projection;
    mat4x4 projection_view;
};

struct scene_t
{
    vec3 ambiance_color;
    float ambiance_strength;
    vec3 sky_color;
    float pad0;
};

#define PI 3.141592653589793238462643383279502884f

// Returns ±1
vec2 sign_not_zero(vec2 v)
{
    return vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
}

vec2 oct_encode(vec3 v)
{
    //Project the sphere onto the octahedron, and then onto the xy plane
    vec2 p = v.xy * (1.0 / (abs(v.x) + abs(v.y) + abs(v.z)));
    // Reflect the folds of the lower hemisphere over the diagonals
    return (v.z <= 0.0) ? ((1.0 - abs(p.yx)) * sign_not_zero(p)) : p;
}

vec3 oct_decode(vec2 e)
{
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0) v.xy = (1.0 - abs(v.yx)) * sign_not_zero(v.xy);
    return normalize(v);
}

vec3 rotate_vector(vec4 q, vec3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

vec3 slerp(vec3 p0, vec3 p1, float t)
{
    float theta = acos(dot(p0, p1));
    vec3 vi = (sin((1.0 - t) * theta) / sin(theta)) * p0;
    vec3 vf = (sin(t * theta) / sin(theta)) * p1;
    return vi + vf;
}

vec3 reinhard_map(vec3 color, float max_white)
{
    vec3 numerator = color * (1.0 +  (color / vec3(max_white * max_white)));
    return numerator / (1.0 + color);
}

float luminance(vec3 v)
{
    return dot(v, vec3(0.2126f, 0.7152f, 0.0722f));
}

vec3 change_luminance(vec3 c_in, float l_out)
{
    float l_in = luminance(c_in);
    return c_in * (l_out / l_in);
}

vec3 reinhard_extended(vec3 v, float max_white)
{
    vec3 numerator = v * (1.0f + (v / vec3(max_white * max_white)));
    return numerator / (1.0f + v);
}

vec3 reinhard_extended_luminance(vec3 v, float max_white_l)
{
    float l_old = luminance(v);
    float numerator = l_old * (1.0f + (l_old / (max_white_l * max_white_l)));
    float l_new = numerator / (1.0f + l_old);
    return change_luminance(v, l_new);
}

vec3 aces_approx(vec3 v)
{
    v *= 0.6f;
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((v*(a*v+b))/(v*(c*v+d)+e), 0.0f, 1.0f);
}

transform_t unpack_transform(in packed_transform_t packed)
{
    transform_t unpacked;
    unpacked.location = packed.location;
    unpacked.scale = packed.scale;

    vec2 rot_wx = unpackSnorm2x16(packed.rotation_wx);
    vec2 rot_yz = unpackSnorm2x16(packed.rotation_yz);

    unpacked.rotation.w = rot_wx[0];
    unpacked.rotation.x = rot_wx[1];
    unpacked.rotation.y = rot_yz[0];
    unpacked.rotation.z = rot_yz[1];

    return unpacked;
}

vec3 world_space_transform(vec3 v, transform_t transform)
{
    v = rotate_vector(transform.rotation, v);
    v *= transform.scale;
    v += transform.location;
    return v;
}

const float max_shadow_bias = 5.0 / 1000.0;
const float constant_shadow_bias = max_shadow_bias / 1000.0;

float in_plane_shadow(in sampler2DShadow shadow, vec3 frag_pos, vec3 frag_norm)
{
    float cos_theta = max(0.0, dot(frag_norm, vec3(0, 0, -1)));
    float attack_bias = max_shadow_bias * (1.0 - cos_theta);
    float bias = clamp(0.0, max_shadow_bias, constant_shadow_bias + attack_bias);

    vec2 sample_coords = frag_pos.xy * 0.5 + 0.5;

    return texture(shadow, vec3(sample_coords, frag_pos.z - bias));
}

float in_cube_shadow(in textureCube shadow_texture, in sampler cube_sampler, vec3 view_pos, vec3 light_pos, float light_strength, vec3 frag_pos, vec3 frag_normal)
{
    vec3 frag_to_light = frag_pos - light_pos;
    float light_distance = length(frag_to_light);
    float depth = light_distance / light_strength;

    if(depth > 1.0)
    {
        return 1.0;
    }

    vec3 from_light_normal = normalize(-frag_to_light);
    float light_hit = dot(frag_normal, from_light_normal);

    if(light_hit < 0.0)
    {
        return 1.0;
    }

    float attack_bias = max_shadow_bias * (1.0 - light_hit);
    float bias = clamp(0.0, max_shadow_bias, constant_shadow_bias + attack_bias);
    depth -= bias;

    return texture(samplerCubeShadow(shadow_texture, cube_sampler), vec4(frag_to_light, depth));
}
