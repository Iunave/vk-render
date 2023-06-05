#version 460

layout(triangles, invocations=6) in;
layout(triangle_strip, max_vertices=3) out;

layout(set=0, binding=0) readonly buffer cube_faces
{
    mat4x4 light_perspectives[6];
};

layout(location=0) out vec4 world_pos;

void main()
{
    gl_Layer = gl_InvocationID;

    for(int vtx = 0; vtx < 3; ++vtx)
    {
        world_pos = gl_in[vtx].gl_Position;
        mat4x4 light_matrix = light_perspectives[gl_InvocationID];
        gl_Position = light_matrix * world_pos;

        EmitVertex();
    }

    EndPrimitive();
}