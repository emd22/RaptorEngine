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

const float PI = 3.14159265359;
// ----------------------------------------------------------------------------
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
// ----------------------------------------------------------------------------
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
// ----------------------------------------------------------------------------

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
    const float metallic = 0.00;

    // calculate reflectance at normal incidence; if dia-electric (like plastic) use F0
    // of 0.04 and if it's a metal, use the albedo color as F0 (metallic workflow)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // START

    const vec3 local_light_pos = a_PC.LightPos.xyz - world_pos;

    vec3 N = normalize(normal);
    vec3 V = normalize(a_PC.PlayerPos.xyz - world_pos);

    const vec3 L = normalize(local_light_pos);
    const vec3 H = normalize(V + L);

    float light_distance = length(local_light_pos);
    float attenuation = 1.0 / (light_distance * light_distance);
    vec3 radiance = a_PC.LightColor.rgb * attenuation;

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;

    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;

    kD *= 1.0 - metallic;

    float NdotL = max(dot(N, L), 0.0);

    v_Color = vec4(radiance, 1.0);
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
