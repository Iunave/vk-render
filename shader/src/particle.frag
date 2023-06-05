#version 460

layout(set=1, binding=0) uniform sampler2D color_tex;

layout(location=0) in vec2 uv;
layout(location=0) out vec4 frag_color;

void main()
{
    vec4 color = texture(color_tex, uv);

    if(color.a == 0.0)
    {
        discard;
    }

    frag_color = color;
}