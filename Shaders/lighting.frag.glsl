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

// Clamped dot product
float ClampedDot(vec3 a, vec3 b) {
    return max(dot(a, b), 1e-5);
}

vec3 WorldPosFromDepth(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);

    vec4 clip = a_PC.InvProjMatrix * ndc;
    vec4 view = a_PC.InvViewMatrix * (clip / clip.w);

    return view.xyz;
}

const float PI = 3.14159265359;
const float ONE_OVER_PI = 0.31830988618;

float D_GGX(float NdotH, float m)
{
    float m2 = m * m;
    float f = (NdotH * m2 - NdotH) * NdotH + 1;
    return m2 / (f * f);
}

// ----------------------------------------------------------------------------
// float DistributionGGX(vec3 N, vec3 H, float roughness)
// {
//     float a_2 = roughness * roughness;
//     float NdotH = ClampedDot(N, H);
//     float denom = (NdotH * NdotH) * (a_2 - 1.0) + 1.0;
//     return (a_2 / (PI * (denom * denom)));

//     // float a = roughness * roughness;
//     // float a2 = a * a;
//     // float NdotH = max(dot(N, H), 0.0);
//     // float NdotH2 = NdotH * NdotH;

//     // float nom = a2;
//     // float denom = (NdotH2 * (a2 - 1.0) + 1.0);
//     // denom = PI * denom * denom;

//     // return nom / denom;
// }

float GeometrySchlickBeckmann(float cos_theta, float K)
{
    return (cos_theta) / (cos_theta * (1.0 - K) + K);
}

float V_SmithGGXCorrelated(float NdotL, float NdotV, float alphaG)
{
    float alphaG2 = alphaG * alphaG;

    float L_GGXV = NdotL * sqrt((-NdotV * alphaG2 + NdotV) * NdotV + alphaG2);
    float L_GGXL = NdotV * sqrt((-NdotL * alphaG2 + NdotL) * NdotL + alphaG2);

    return 0.5f / (L_GGXV + L_GGXL);
}

// float GeometrySmith(float NdotV, float NdotL, float roughness)
// {
//     float rn2 = roughness + 1.0;
//     float K = (rn2 * rn2) / 8.0;
//     return GeometrySchlickBeckmann(NdotV, K) * GeometrySchlickBeckmann(NdotL, K);
// }

// ----------------------------------------------------------------------------
// float GeometrySchlickGGX(float NdotV, float roughness)
// {
//     float r = (roughness + 1.0);
//     float k = (r * r) / 8.0;

//     float nom = NdotV;
//     float denom = NdotV * (1.0 - k) + k;

//     return nom / denom;
// }
// // ----------------------------------------------------------------------------
// float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
// {
//     float NdotL = max(dot(N, L), 0.0);
//     float ggx1 = GeometrySchlickGGX(NdotL, roughness);

//     float NdotV = max(dot(N, V), 0.0);
//     float ggx2 = GeometrySchlickGGX(NdotV, roughness);

//     return ggx1 * ggx2;
// }
// ----------------------------------------------------------------------------
vec3 F_Schlick(vec3 f0, float f90, float u)
{
    return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}
// ----------------------------------------------------------------------------

float Fr_DisneyDiffuse(float NdotV, float NdotL, float LdotH, float linear_roughness)
{
    float energy_bias = 0.5 * linear_roughness;
    float energy_factor = mix(1.0, 1.0 / 1.51, linear_roughness);

    float fd90_minus_one = energy_bias + 2.0 * LdotH * LdotH * linear_roughness - 1.0;

    float light_scatter = 1.0 + (fd90_minus_one * pow(1.0 - NdotL, 5.0));
    float view_scatter = 1.0 + (fd90_minus_one * pow(1.0 - NdotV, 5.0));

    return light_scatter * view_scatter * energy_factor;

    // float energy_bias = mix(0, 0.5, linear_roughness);
    // float energy_factor = mix(1.0, 1.0 / 1.51, linear_roughness);

    // float fd90 = energy_bias + 2.0 * LdotH * LdotH * linear_roughness;
    // vec3 f0 = vec3(1.0);
    // float light_scatter = F_Schlick(f0, fd90, NdotL).r;
    // float view_scatter = F_Schlick(f0, fd90, NdotV).r;

    // return light_scatter * view_scatter * energy_factor;
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

    const float roughness = 0.1;
    const float metallic = 0.9;

    // calculate reflectance at normal incidence; if dia-electric (like plastic) use F0
    // of 0.04 and if it's a metal, use the albedo color as F0 (metallic workflow)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // START

    vec3 local_light_pos = a_PC.LightPos.xyz - world_pos;

    vec3 N = normalize(normal);
    vec3 V = normalize(a_PC.PlayerPos.xyz - world_pos);

    const vec3 L = normalize(local_light_pos);
    const vec3 H = normalize(V + L);

    float NdotL = ClampedDot(N, L);
    float NdotV = abs(dot(N, V)) + 1e-5f;
    float NdotH = ClampedDot(N, H);
    float LdotH = ClampedDot(L, H);

    float light_distance = length(local_light_pos);

    const float light_intensity = 1.0f;

    float attenuation = light_intensity * (1.0 / max(light_distance * light_distance, 1e-4));

    // vec3 light_radiance = albedo *

    vec3 F = F_Schlick(F0, 1.0, LdotH);

    vec3 diffuse_reflectance = albedo * (1.0 - metallic);

    float D = D_GGX(NdotH, roughness);
    float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
    // float Vis = GeometrySmith(NdotV, NdotL, roughness);
    // vec3 F = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);

    vec3 Fr = D * F * Vis * ONE_OVER_PI;

    float linear_roughness = roughness * roughness;

    float Fd = Fr_DisneyDiffuse(NdotV, NdotL, LdotH, linear_roughness);

    v_Color = vec4(attenuation * (Fd * albedo + Fr) * a_PC.LightColor.rgb * NdotL * ONE_OVER_PI, normal_rgba.a);

    // v_Color = vec4(vec3(attenuation), 1.0);
    // v_Color = vec4((kD * albedo / PI + specular) * radiance * NdotL, normal_rgba.a);

    // vec3 light_dir = a_PC.LightPos.xyz - world_pos;

    // float fdist = length(light_dir);
    // light_dir = normalize(light_dir);

    // vec3 view_dir = normalize(a_PC.PlayerPos.xyz - world_pos);
    // vec3 reflect_dir = reflect(-light_dir, normal);

    // float spec = pow(max(dot(view_dir, reflect_dir), 0.0), 32);

    // vec3 pl_color = a_PC.LightColor.rgb;

    // float specular_strength = 0.5;
    // vec3 specular = specular_strength * spec * pl_color.rgb;

    // float fdiffuse = max(0.0, dot(normal, light_dir));

    // float att_const = 0.35;
    // float att_lin = 0.005;
    // float att_expr = 0.02;
    // float fatt = att_const + att_lin * fdist + att_expr * fdist * fdist;

    // if (a_PC.LightRadius == 0) {
    //     fatt = 1.0f;
    // }

    // float ambient = 0.05;

    // vec3 result = vec3(pl_color) / fatt;
    // // vec3 result = (pl_color * (ambient + fdiffuse + specular) / fatt);

    // v_Color = vec4(result, normal_rgba.a);
}
