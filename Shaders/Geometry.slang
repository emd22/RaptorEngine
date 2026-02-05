module Geometry;

#include "./Object.slang.inc"

//////////////////////////////////
// Vertex shader
//////////////////////////////////

struct VSOut
{
    float4 vPosition : SV_POSITION;
    float3 vNormalWS   : NORMAL;
    float2 vUV       : TEXCOORD0;
#ifdef USE_NORMAL_MAPS
    float3 vTangentWS : TANGENT;
    float3 vBitangentWS : BITANGENT;
#endif
};

struct VSIn
{
    float3 vPosition : POSITION;
    float3 vNormal : NORMAL;
    float2 vUV : TEXCOORD0;
    float3 vTangent : TANGENT;
    uint uiInstanceId : SV_InstanceID;
};

struct VSPushConsts
{
    float4x4 mViewProjection;
	uint uiObjectIndex;
    uint uiMaterialIndex;
};

layout(set = 2, binding = 0) StructuredBuffer<Object> bObjectBuffer;

[[vk::push_constant]] VSPushConsts VSConst;

[shader("vertex")]

VSOut VertexMain(VSIn in)
{
    VSOut out;

    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + in.uiInstanceId].mModel;

    float4x4 MVP = mul(VSConst.mViewProjection, model_matrix);

    out.vPosition = mul(MVP, float4(in.vPosition, 1.0));
    out.vNormalWS = normalize(mul(float3x3(model_matrix), in.vNormal));

#ifdef USE_NORMAL_MAPS
    out.vTangentWS = normalize(mul(float3x3(model_matrix), in.vTangent));
    out.vBitangentWS = cross(out.vNormalWS, out.vTangentWS);
#endif

    out.vUV = in.vUV;

    return out;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

#include "./MaterialProperties.slang.inc"

struct FSOut {
    float4 vAlbedo : SV_TARGET0;
    float4 vNormal : SV_TARGET1;
};

struct FSIn
{
    float3 vNormalWS : NORMAL;
    float2 vUV : TEXCOORD0;

#ifdef USE_NORMAL_MAPS
    float3 vTangentWS : TANGENT;
    float3 vBitangentWS : BITANGENT;
#endif
};

layout(set = 0, binding = 0) Sampler2D sAlbedo;

#ifdef USE_NORMAL_MAPS
layout(set = 0, binding = 1) Sampler2D sNormalMap;
layout(set = 0, binding = 2) Sampler2D sMetallicRoughness;
#endif

layout(set = 1, binding = 0) StructuredBuffer<Material> bMaterialBuffer;

[shader("fragment")]

FSOut FragmentMain(FSIn in)
{
    FSOut out;

    float4 material_color = unpackUnorm4x8ToFloat(bMaterialBuffer[VSConst.uiMaterialIndex].uiBaseColor);
    out.vAlbedo = float4(sAlbedo.Sample(in.vUV).rgb, 1.0) + material_color;

#ifdef USE_NORMAL_MAPS
    float2 roughness_metallic = sMetallicRoughness.Sample(in.vUV).gb;

    float3 normal_ts = sNormalMap.Sample(in.vUV).rgb;
    normal_ts = normal_ts * 2.0 - 1.0;

    float3x3 TBN = float3x3(in.vTangentWS, in.vBitangentWS, in.vNormalWS);

    float3 normal_ws = mul(normal_ts, TBN);

    // Roughness
    out.vNormal = float4(normalize(normal_ws), roughness_metallic.x);

    // Metalness
    out.vAlbedo.w = roughness_metallic.y;
#else
    out.vNormal = float4(in.vNormalWS, 0.5);
#endif


    return out;
}
