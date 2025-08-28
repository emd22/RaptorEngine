#version 450

////////////////////////
// Outputs
////////////////////////
//
layout(location = 0) out vec3 v_UVW;

////////////////////////
// Inputs
////////////////////////

layout(location = 0) in vec3 a_Position;

layout(push_constant) uniform PushConstants
{
    // 64
    mat4 ProjectionMatrix;

    // 128
    mat4 ModelMatrix;
} a_PushConsts;

void main()
{
    v_UVW = a_Position;

    // Convert cubemap coordinates into Vulkan coordinate space
    v_UVW.xy *= -1.0;

    // Remove translation from view matrix
    mat4 skybox_view_matrix = mat4(mat3(a_PushConsts.ModelMatrix));

    gl_Position = a_PushConsts.ProjectionMatrix * skybox_view_matrix * vec4(a_Position, 1.0);
}
