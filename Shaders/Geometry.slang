module Geometry;

#define LIGHTING_USE_NORMALMAPPING 1

//////////////////////////////////
// Vertex shader
//////////////////////////////////

struct VSOut
{
    float4 vPosition : SV_POSITION;
    float3 vNormalWS   : NORMAL;
    float2 vUV       : TEXCOORD0;
    float3 vTangentWS : TANGENT;
};

struct VSIn
{
    float3 vPosition : POSITION;
    float3 vNormal : NORMAL;
    float2 vUV : TEXCOORD0;
    float3 vTangent : TANGENT;
};

struct VSPushConsts
{
    float4x4 mMVP;
    float3x3 mNormalMatrix;
    uint uiMaterialIndex;
};

[[vk::push_constant]] VSPushConsts VSConst;

[shader("vertex")]

VSOut VertexMain(VSIn in)
{
    VSOut out;

    out.vPosition = mul(float4(in.vPosition, 1.0), VSConst.mMVP);

    #ifdef LIGHTING_USE_NORMALMAPPING
    out.vTangentWS = normalize(mul(in.vTangent, VSConst.mNormalMatrix));

    #else
    out.vTangentWS = float3(0);
    #endif

    out.vNormalWS = normalize(mul(in.vNormal, VSConst.mNormalMatrix));
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
    float3 vTangentWS : TANGENT;

};

layout(set = 0, binding = 0) Sampler2D sAlbedo;
layout(set = 0, binding = 1) Sampler2D sNormalMap;
layout(set = 1, binding = 0) StructuredBuffer<Material> bMaterialBuffer;

[shader("fragment")]

FSOut FragmentMain(FSIn in)
{
    FSOut out;

    float4 material_color = unpackUnorm4x8ToFloat(bMaterialBuffer[VSConst.uiMaterialIndex].uiBaseColor);
    out.vAlbedo = float4(sAlbedo.Sample(in.vUV).rgb, 1.0) + material_color;

    #ifdef LIGHTING_USE_NORMALMAPPING

    float3 bitangent_ws = cross(in.vNormalWS, in.vTangentWS);


    float3 normal_ts = sNormalMap.Sample(in.vUV).rgb;
    normal_ts = normal_ts * 2.0 - 1.0;

    float3x3 TBN = float3x3(in.vTangentWS, bitangent_ws, in.vNormalWS);

    float3 normal_ws = mul(normal_ts, TBN);
    out.vNormal = float4(normalize(normal_ws), 1.0);

    #else
    out.vNormal = float4(in.vNormalWS, 1.0f);
    #endif


    return out;
}
