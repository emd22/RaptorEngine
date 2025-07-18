#version 450

layout(location = 0) out vec2 v_UV;

layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform VertMatrices {
    mat4 Mvp;
} a_VertMatrices;

void main()
{
    v_UV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = a_VertMatrices.Mvp * vec4(a_Position, 1.0);
}
