#version 450

layout(location = 0) in vec3 a_Position;

layout(location = 0) out vec3 v_LightPos;
layout(location = 1) out float v_LightRadius;
layout(location = 2) out vec3 v_PlayerPos;

layout(push_constant) uniform VertMatrices
{
    mat4 Mvp;
    mat4 VPMatrix;
    vec3 LightPos;
    vec3 PlayerPos;
    float LightRadius;
} a_VertMatrices;

void main()
{
    // vec4 pos = a_VertMatrices.Mvp * vec4(a_Position, 1.0);
    // gl_Position = pos;

    vec4 clip_pos = a_VertMatrices.Mvp * vec4(a_Position, 1.0);
    // v_Position = clip_pos;
    gl_Position = clip_pos;

    // vec3 vertex_eye_pos = a_Position * a_VertMatrices.LightRadius + a_LightPos;

    v_PlayerPos = a_VertMatrices.PlayerPos;
    v_LightRadius = a_VertMatrices.LightRadius;
    // a_LightPos = a_VertMatrices.LightPos;
    v_LightPos = (a_VertMatrices.LightPos);
}
