module Lighting;

#include "./Object.slang.inc"

//////////////////////////////////
// Vertex shader
//////////////////////////////////

struct VSIn
{
#ifdef FX_LIGHT_DIRECTIONAL
    int iVertexIndex : SV_VertexID;
#else
    float3 vPosition : ATTR0;
#endif
};


struct VSPushConsts
{
    float4x4 mViewProjection;
    uint uiObjectIndex;
};

[[vk::push_constant]] VSPushConsts VSConst;

layout(binding = 4) StructuredBuffer<Object> bObjectBuffer;

[shader("vertex")]

float4 VertexMain(VSIn in) : SV_POSITION
{
#ifdef FX_LIGHT_DIRECTIONAL
    float2 out_uv = float2((in.iVertexIndex << 1) & 2, in.iVertexIndex & 2);
    return float4(out_uv * 2.0 - 1.0, 0.0, 1.0);
#else
    float4x4 MVP = mul(VSConst.mViewProjection, bObjectBuffer[VSConst.uiObjectIndex].mModel);
    return mul(MVP, float4(in.vPosition, 1.0));
#endif
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

struct FSIn
{
    float4 vPosition: SV_POSITION;
};

struct FSOut
{
    float4 vColor : SV_TARGET0;
};

layout(binding = 1) Sampler2D sDepth;
layout(binding = 2) Sampler2D sAlbedo;
layout(binding = 3) Sampler2D sNormal;

struct FSPushConsts
{
    [[vk::offset(80)]]
    float4x4 mInvView;
    float4x4 mInvProjection;
    float3 vEyePosition;
    float fLightRadius;
    float3 vLightPosition;
    uint uiLightColor;
};

[[vk::push_constant]] FSPushConsts FSConst;


#define FX_MATH_PI 3.14159265359
#define FX_MATH_1_OVER_PI 0.31830988618

float DotC(float3 a, float3 b)
{
    return max(dot(a, b), 1e-5);
}


float D_GGX(float NdotH, float m)
{
    float m2 = m * m;
    float f = (NdotH * m2 - NdotH) * NdotH + 1.0;
    return m2 / (f * f);
}

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

float3 F_Schlick(float3 f0, float f90, float u)
{
    return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}


float Fr_FrostbiteDisneyDiffuse(float NdotV, float NdotL, float LdotH, float linear_roughness)
{
    float energy_bias = 0.5 * linear_roughness;
    float energy_factor = lerp(1.0, 1.0 / 1.51, linear_roughness);

    float fd90_minus_one = energy_bias + 2.0 * LdotH * LdotH * linear_roughness - 1.0;

    float light_scatter = 1.0 + (fd90_minus_one * pow(1.0 - NdotL, 5.0));
    float view_scatter = 1.0 + (fd90_minus_one * pow(1.0 - NdotV, 5.0));

    return light_scatter * view_scatter * energy_factor;
}

float3 WorldPosFromDepth(float2 uv, float depth)
{
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);

    float4 view_space = mul(FSConst.mInvProjection, ndc);
    float4 world_space = mul(FSConst.mInvView, (view_space / view_space.w));

    return world_space.xyz;
}

float AttenuationSmooth(float distance_sq, float inv_radius_sq)
{
    float factor = distance_sq * inv_radius_sq;
    float smooth_factor = saturate(1.0 - factor * factor);

    return (smooth_factor * smooth_factor) / max(distance_sq, 1e-4);

}


[shader("fragment")]

FSOut FragmentMain(FSIn in)
{
    FSOut out;

    float2 screen_uv = in.vPosition.xy / float2(1024, 720);

    float depth = 1.0 - sDepth.Sample(screen_uv).r;
    float3 albedo = sAlbedo.Sample(screen_uv).rgb;
    float4 normal_rgba = sNormal.Sample(screen_uv);

    float3 world_position = WorldPosFromDepth(screen_uv, depth);

    float4 light_color = unpackUnorm4x8ToFloat(FSConst.uiLightColor);
    // float4 light_color = float4(1, 0, 0, 1);

    let roughness : float = 0.1;
    let metallic : float = 0.9;

    float light_intensity = 15;

    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);

#ifdef FX_LIGHT_DIRECTIONAL
// float3 light_position_local = FSConst.vLightPosition - world_position;

    float3 L = normalize(FSConst.vLightPosition);
#else
    float3 light_position_local = FSConst.vLightPosition - world_position;
    float3 L = normalize(light_position_local);
#endif
    float3 N = normalize(normal_rgba.rgb);
    float3 V = normalize(FSConst.vEyePosition - world_position);
    float3 H = normalize(V + L);

    float NdotL = DotC(N, L);
    float NdotV = abs(dot(N, V)) + 1e-5f;
    float NdotH = DotC(N, H);
    float LdotH = DotC(L, H);

#ifdef FX_LIGHT_DIRECTIONAL
    let attenuation: float = light_intensity;
#else
    float dist_sq = dot(light_position_local, light_position_local);
    float light_distance = sqrt(dist_sq);

    float inv_radius_sq = 1.0 / (FSConst.fLightRadius * FSConst.fLightRadius);
    float attenuation = light_intensity * AttenuationSmooth(dist_sq, inv_radius_sq);
    // float attenuation = light_intensity * (1.0 / max(light_distance * light_distance, 1e-5));
#endif

    float3 F = F_Schlick(F0, 1.0, LdotH);
    float3 diffuse_reflectance = albedo * (1.0 - metallic);

    float D = D_GGX(NdotH, roughness);
    float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
    float3 Fr = D * F * Vis * FX_MATH_1_OVER_PI;

    float Fd = Fr_FrostbiteDisneyDiffuse(NdotV, NdotL, LdotH, (roughness * roughness));

    float3 diffuse_term = Fd * diffuse_reflectance * FX_MATH_1_OVER_PI;
    float3 specular_term = Fr;

    out.vColor = float4(attenuation * (diffuse_term + specular_term) * light_color.rgb * NdotL, 1.0);

    return out;
}
