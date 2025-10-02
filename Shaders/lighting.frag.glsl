#version 450

#include "MaterialProperties.glsl.inc"

////////////////////////
// Outputs
////////////////////////

layout(location = 0) out vec4 v_Color;

////////////////////////
// Inputs
////////////////////////

layout(binding = 1) uniform sampler2D s_Depth;
layout(binding = 2) uniform sampler2D s_Albedo;
layout(binding = 3) uniform sampler2D s_Normals;

layout(push_constant) uniform PushConstants
{
    layout(offset = 64) mat4 InvViewMatrix; // 128
    mat4 InvProjMatrix; // 192
    vec4 LightPos;
    vec4 LightColor;
    vec4 PlayerPos;
    float LightRadius;
} a_PC;

// A list of all materials loaded into the GPU
// layout(set = 2, binding = 1) readonly buffer MaterialPropertiesBO
// {
//     MaterialProperties Materials[];
// } mats;

vec3 WorldPosFromDepth(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);

    vec4 clip = a_PC.InvProjMatrix * ndc;
    vec4 view = a_PC.InvViewMatrix * (clip / clip.w);

    return view.xyz;
}

void main()
{
    vec2 screen_uv = gl_FragCoord.xy / vec2(1024, 720);

    // screen_uv.y = 1.0 - screen_uv.y;

    float inv_depth = texture(s_Depth, screen_uv).r;
    float depth = 1.0 - inv_depth;

    vec3 albedo = texture(s_Albedo, screen_uv).rgb;
    vec4 normal_rgba = texture(s_Normals, screen_uv);
    vec3 normal = normal_rgba.rgb;

    vec3 world_pos = WorldPosFromDepth(screen_uv, depth);

    vec3 light_dir = a_PC.LightPos.xyz - world_pos;

    float fdist = length(light_dir);
    light_dir = normalize(light_dir);

    vec3 view_dir = normalize(a_PC.PlayerPos.xyz - world_pos);
    vec3 reflect_dir = reflect(-light_dir, normal);

    float spec = pow(max(dot(view_dir, reflect_dir), 0.0), 32);

    vec3 pl_color = a_PC.LightColor.rgb;

    float specular_strength = 0.5;
    vec3 specular = specular_strength * spec * pl_color.rgb;

    float fdiffuse = max(0.0, dot(normal, light_dir));

    float att_const = 0.35;
    float att_lin = 0.1;
    float att_expr = 0.02;
    float fatt = att_const + att_lin * fdist + att_expr * fdist * fdist;

    if (a_PC.LightRadius == 0) {
        fatt = 1.0f;
    }

    float ambient = 0.05;

    vec3 result = vec3(pl_color) / fatt;
    // vec3 result = (pl_color * (ambient + fdiffuse + specular) / fatt);

    v_Color = vec4(result, normal_rgba.a);
}
