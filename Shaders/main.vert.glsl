#version 450

#include "MaterialProperties.glsl.inc"

////////////////////////
// Outputs
////////////////////////

layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec2 v_UV;

layout(location = 2) out uint v_MaterialIndex;

////////////////////////
// Inputs
////////////////////////

layout(binding = 1) uniform UniformBufferObject
{
    mat4 MvpMatrix;
} Ubo;

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;

layout(push_constant) uniform PushConstants
{
    mat4 MvpMatrix;
    mat4 ModelMatrix;

    uint MaterialIndex;
} a_PushConsts;

void main()
{
    gl_Position = a_PushConsts.MvpMatrix * vec4(a_Position, 1.0);
    mat3 normal_matrix = mat3(transpose(inverse(a_PushConsts.ModelMatrix)));

    v_Normal = (normal_matrix * a_Normal);
    v_UV = a_UV;

    v_MaterialIndex = a_PushConsts.MaterialIndex;
}
