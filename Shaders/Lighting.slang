module Lighting;

//////////////////////////////////
// Vertex shader
//////////////////////////////////

struct VSIn
{
    float3 vPosition : ATTR0;
};


struct VSPushConsts
{
    float4x4 mMVP;
};

[[vk::push_constant]] VSPushConsts VSConst;

[shader("vertex")]

float4 VertexMain(VSIn in) : SV_POSITION
{
    return mul(float4(in.vPosition, 1.0), VSConst.mMVP);
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
    [[vk::offset(64)]]
    float4x4 mInvView;
    float4x4 mInvProjection;
    float4 vLightPosition;
    float4 vEyePosition;
    float fLightRadius;
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

    float4 clip = mul(ndc, FSConst.mInvProjection);
    float4 view = mul((clip / clip.w), FSConst.mInvView);

    return view.xyz;
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

    let roughness : float = 0.1;
    let metallic : float = 0.9;

    let light_intensity : float = 50.0;


    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);


    float3 light_position_local = FSConst.vLightPosition.xyz - world_position;

    float3 N = normalize(normal_rgba.rgb);
    float3 V = normalize(FSConst.vEyePosition.xyz - world_position);
    float3 L = normalize(light_position_local);
    float3 H = normalize(V + L);

    float NdotL = DotC(N, L);
    float NdotV = abs(dot(N, V)) + 1e-5f;
    float NdotH = DotC(N, H);
    float LdotH = DotC(L, H);

    float light_distance = length(light_position_local);

    float attenuation = light_intensity * (1.0 / max(light_distance * light_distance, 1e-5));

    float3 F = F_Schlick(F0, 1.0, LdotH);
    float3 diffuse_reflectance = albedo * (1.0 - metallic);

    float D = D_GGX(NdotH, roughness);
    float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
    float3 Fr = D * F * Vis * FX_MATH_1_OVER_PI;

    float Fd = Fr_FrostbiteDisneyDiffuse(NdotV, NdotL, LdotH, (roughness * roughness));

    float4 light_color = unpackUnorm4x8ToFloat(FSConst.uiLightColor);

    out.vColor = float4(attenuation * (Fd * albedo + Fr) * light_color.rgb * NdotL * FX_MATH_1_OVER_PI, normal_rgba.a);

    return out;
}
