
#include "Util.hlsl"

///////////////////////////////////
// Vertex Shader
///////////////////////////////////

F_PARAMTEST(FPT_VERTEX, 10, FR_STRUCTBUFFER)


F_PROGRAM(FPT_VERTEX)

struct VSInput
{
    float3 vPosition : POSITION;
    float3 vNormal : NORMAL;
    float2 vUV : TEXCOORD0;
    float3 vTangent : TANGENT;
    uint uiInstanceId : SV_InstanceID;
#ifdef USE_SKINNING
    float4 vJointIndices : ATTR0;
    float4 vJointWeights : ATTR1;
#endif
};

struct VSOutput
{
    float4 vPosition : SV_POSITION;
    float3 vNormalWS   : NORMAL;
    float2 vUV       : TEXCOORD0;
#ifdef USE_NORMAL_MAPS
    float3 vTangentWS : TANGENT;
    float3 vBitangentWS : BITANGENT;
#endif
};

struct VSPushConsts
{
    float4x4 mViewProjection;
	uint uiObjectIndex;
    uint uiMaterialIndex;
};

F_REFLECT(FR_STRUCTBUFFER, 2, 0)
[[vk::binding(2, 0)]] StructuredBuffer<Object> bObjectBuffer;

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput in)
{
    VSOutput out;

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

///////////////////////////////////
// Pixel Shader
///////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSOutput {
    float4 vAlbedo : SV_TARGET0;
    float4 vNormal : SV_TARGET1;
};

struct FSInput
{
    float3 vNormalWS : NORMAL;
    float2 vUV : TEXCOORD0;

#ifdef USE_NORMAL_MAPS
    float3 vTangentWS : TANGENT;
    float3 vBitangentWS : BITANGENT;
#endif
};

[[vk::binding(0, 0)]] sampler2D sAlbedo;

#ifdef USE_NORMAL_MAPS
[[vk::binding(0, 1)]] sampler2D sNormalMap;
[[vk::binding(0, 2)]] sampler2D sMetallicRoughness;
#endif


layout(set = 1, binding = 0) StructuredBuffer<Material> bMaterialBuffer;

FSOutput main(FSInput in)
{
    FSOutput out;

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
