#version 450

////////////////////////
// Outputs
////////////////////////

layout(location = 0) out vec4 v_Albedo;

////////////////////////
// Inputs
////////////////////////

layout(location = 0) in vec3 a_UVW;

layout(set = 0, binding = 0) uniform samplerCube s_Sky;

void main()
{
    v_Albedo = texture(s_Sky, a_UVW);
}
