#version 450

#include "MaterialProperties.glsl.inc"

////////////////////////
// Outputs
////////////////////////

layout(location = 0) out vec4 v_Albedo;
layout(location = 1) out vec4 v_Normal;

// Material index
// layout(location = 2) out int v_Material;

////////////////////////
// Inputs
////////////////////////

layout(location = 0) in vec3 a_Normal;
layout(location = 1) in vec2 a_UV;
layout(location = 2) flat in uint a_MaterialIndex;

layout(set = 0, binding = 0) uniform sampler2D s_Albedo;

// A list of all materials loaded into the GPU
layout(set = 1, binding = 0) readonly buffer MaterialPropertiesBO
{
    MaterialProperties Materials[];
} mats;

void main() {
    vec4 mat_color = unpackUnorm4x8(mats.Materials[a_MaterialIndex].BaseColor);
    v_Albedo = vec4(texture(s_Albedo, a_UV).rgb, 1.0) + mat_color;
    v_Normal = vec4(a_Normal, 1.0f);
}
