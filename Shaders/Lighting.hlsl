#include "./Helper.hlsl"

//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct VSInput
{
#ifdef FX_LIGHT_DIRECTIONAL
    int iVertexIndex : SV_VertexID;
#else
    float3 vPosition : ATTR0;
#endif
};

struct VSOutput
{
    float4 vPosition : SV_POSITION;
    uint uiLightIndex : ATTR0;
};


struct VSPushConsts
{
    float4x4 CameraMatrix;
    uint uiObjectIndex;
    uint uiLightIndex;
};

[[vk::push_constant]] VSPushConsts VSConst;

F_REFLECT(FR_STRUCTBUFFER, 0, 2);
[[vk::binding(0, 2)]] StructuredBuffer<Object> bObjectBuffer;

VSOutput main(VSInput input)
{
    VSOutput output;

#ifdef FX_LIGHT_DIRECTIONAL

    float2 out_uv = float2((input.iVertexIndex << 1) & 2, input.iVertexIndex & 2);
    output.vPosition = float4(out_uv * 2.0 - 1.0, 0.0, 1.0);
#else
    float4x4 mvp = mul(VSConst.CameraMatrix, bObjectBuffer[VSConst.uiObjectIndex].mModel);
    output.vPosition = mul(mvp, float4(input.vPosition, 1.0));
#endif

    output.uiLightIndex = VSConst.uiLightIndex;

    return output;
}


//////////////////////////////////
// Pixel shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSInput
{
    float4 vPosition : SV_POSITION;
    uint uiLightIndex : ATTR0;
};

struct FSOutput
{
    float4 vColor : SV_TARGET0;
};

F_Texture2D(tDepth, 0);
F_Texture2D(tAlbedo, 1);
F_Texture2D(tNormal, 2);
F_ShadowTexture2D(tShadow, 3);

struct Light
{
    // 64
    float4x4 LightCameraMatrix;
    // 128
    float4x4 mInvView;
    // 192
    float4x4 mInvProjection;
    // 208
    float3 vEyePosition;
    float1 fLightRadius;
    // 224
    float3 vLightPosition;
    uint1 uiLightColor;
    // 236
    float2 vCameraSize;
    uint1 uiAmbient;
};

F_REFLECT(FR_CBUFFER, 4, 0);
[[vk::binding(4, 0)]] cbuffer FSUniforms
{
    Light Lights[LIGHT_COUNT];
};

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

float3 WorldPosFromDepth(Light light, float2 uv, float depth)
{
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);

    float4 world_space = mul(mul(light.mInvView, light.mInvProjection), ndc);

    return world_space.xyz / world_space.w;
}

float AttenuationSmooth(float distance_sq, float inv_radius_sq)
{
    float factor = distance_sq * inv_radius_sq;
    float smooth_factor = saturate(1.0 - factor * factor);

    return (smooth_factor * smooth_factor) / max(distance_sq, 1e-4);

}

FSOutput main(FSInput input)
{
    FSOutput output;

    Light light = Lights[input.uiLightIndex];

    float2 screen_uv = input.vPosition.xy / light.vCameraSize;

    float depth = 1.0 - F_Sample(tDepth, screen_uv).r;
    float4 albedo_rgba = F_Sample(tAlbedo, screen_uv);
    float3 albedo = albedo_rgba.rgb;


    float4 normal_rgba = F_Sample(tNormal, screen_uv);
    float3 world_position = WorldPosFromDepth(light, screen_uv, depth);

    float4 light_color = F_UnpackUIntToFloat4(light.uiLightColor);

    float roughness = normal_rgba.w;
    float metallic = albedo_rgba.w;

    float light_intensity = light_color.w * 255.0;

    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);

    float visibility = 1.0f;

#ifdef FX_LIGHT_DIRECTIONAL
    float4 shadow_pos_light_space = mul(light.LightCameraMatrix, float4(world_position, 1.0));

    float2 shadow_uv;
    shadow_uv.x = 0.5f + (shadow_pos_light_space.x / shadow_pos_light_space.w * 0.5f);
    shadow_uv.y = 0.5f - (shadow_pos_light_space.y / shadow_pos_light_space.w * 0.5f);
    shadow_uv.y = 1.0 - shadow_uv.y;

    float shadow_z = 1.0 - shadow_pos_light_space.z / shadow_pos_light_space.w;

    // Check that the UV values are greater than 0.0 and less than 1.0
    if ((saturate(shadow_uv.x) == shadow_uv.x) && (saturate(shadow_uv.y) == shadow_uv.y) && (shadow_z > 0)) {
        visibility = F_SampleCmpLevelZero(tShadow, shadow_uv, shadow_z + 0.001f);
        visibility = clamp(visibility, 0.05f, 1.0f);
    }

    float3 L = normalize(light.vLightPosition);
#else
    float3 light_position_local = light.vLightPosition - world_position;
    float3 L = normalize(light_position_local);
#endif
    float3 N = normalize(normal_rgba.rgb);
    float3 V = normalize(light.vEyePosition - world_position);
    float3 H = normalize(V + L);

    float NdotL = DotC(N, L);
    float NdotV = abs(dot(N, V)) + 1e-5f;
    float NdotH = DotC(N, H);
    float LdotH = DotC(L, H);

#ifdef FX_LIGHT_DIRECTIONAL
    const float attenuation = light_intensity;
#else
    float dist_sq = dot(light_position_local, light_position_local);
    float light_distance = sqrt(dist_sq);

    float inv_radius_sq = 1.0 / (light.fLightRadius * light.fLightRadius);
    float attenuation = light_intensity * AttenuationSmooth(dist_sq, inv_radius_sq);
#endif

    float3 F = F_Schlick(F0, 1.0, LdotH);
    float3 diffuse_reflectance = albedo * (1.0 - metallic);

    float D = D_GGX(NdotH, roughness);
    float Vis = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
    float3 Fr = D * F * Vis * FX_MATH_1_OVER_PI;

    float Fd = Fr_FrostbiteDisneyDiffuse(NdotV, NdotL, LdotH, (roughness * roughness));

    float3 diffuse_term = Fd * diffuse_reflectance * FX_MATH_1_OVER_PI;
    float3 specular_term = Fr;

    float4 ambient = F_UnpackUIntToFloat4(light.uiAmbient) * float4(albedo, 1.0f);

    output.vColor = float4(attenuation * (visibility * diffuse_term + visibility * specular_term) * light_color.rgb * NdotL + ambient.rgb, 1.0);

    return output;
}
