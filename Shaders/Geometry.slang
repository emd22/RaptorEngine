module Geometry;

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
};

struct VSPushConsts
{
    float4x4 mViewProjection;
	uint uiObjectIndex;
    uint uiMaterialIndex;
};

struct Object
{
	float4x4 mModel; 
}

layout(set = 0, binding = 0) StructuredBuffer<Object> bObjectBuffer;

[[vk::push_constant]] VSPushConsts VSConst;

[shader("vertex")]

VSOut VertexMain(VSIn in)
{
    VSOut out;

    float4x4 MVP = mul(VSConst.mModelMatrix, VSConst.mViewProjection);

    out.vPosition = mul(float4(in.vPosition, 1.0), MVP);

    out.vNormalWS = normalize(mul(in.vNormal, float3x3(VSConst.mModelMatrix)));

#ifdef USE_NORMAL_MAPS
    out.vTangentWS = normalize(mul(in.vTangent, float3x3(VSConst.mModelMatrix)));
    out.vBitangentWS =  cross(out.vNormalWS, out.vTangentWS);
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

// #ifdef USE_NORMAL_MAPS
layout(set = 0, binding = 1) Sampler2D sNormalMap;
// #endif

layout(set = 1, binding = 0) StructuredBuffer<Material> bMaterialBuffer;

[shader("fragment")]

FSOut FragmentMain(FSIn in)
{
    FSOut out;

    float4 material_color = unpackUnorm4x8ToFloat(bMaterialBuffer[VSConst.uiMaterialIndex].uiBaseColor);
    out.vAlbedo = float4(sAlbedo.Sample(in.vUV).rgb, 1.0) + material_color;

#ifdef USE_NORMAL_MAPS
    float3 normal_ts = sNormalMap.Sample(in.vUV).rgb;
    normal_ts = normal_ts * 2.0 - 1.0;

    float3x3 TBN = float3x3(in.vTangentWS, in.vBitangentWS, in.vNormalWS);

    float3 normal_ws = mul(normal_ts, TBN);
    out.vNormal = float4(normalize(normal_ws), 1.0);

#else
    out.vNormal = float4(in.vNormalWS, 1.0f);
#endif


    return out;
}
