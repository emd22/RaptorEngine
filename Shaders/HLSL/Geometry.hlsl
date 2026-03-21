
#include "./Helper.hlsl"


///////////////////////////////////
// Vertex Shader
///////////////////////////////////

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

//F_REFLECT(FR_STRUCTBUFFER, 2, 0)
[[vk::binding(0, 2)]] StructuredBuffer<Object> bObjectBuffer;

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
    VSOutput output;

    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mModel;

    float4x4 MVP = mul(VSConst.mViewProjection, model_matrix);

    output.vPosition = mul(MVP, float4(input.vPosition, 1.0));
    output.vNormalWS = normalize(mul((float4x3)model_matrix, input.vNormal));

#ifdef USE_NORMAL_MAPS
    output.vTangentWS = normalize(mul((float3x3)model_matrix, input.vTangent));
    output.vBitangentWS = cross(output.vNormalWS, output.vTangentWS);
#endif

    output.vUV = input.vUV;

    return output;
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

//F_REFLECT(FR_SAMPLER2D, 0, 0)

F_Texture2D(tAlbedo, 0)

#ifdef USE_NORMAL_MAPS
F_Texture2D(tNormalMap, 1)
F_Texture2D(tMetallicRoughness, 2)
#endif


//F_REFLECT(FR_STRUCTBUFFER, 1, 0)
[[vk::binding(0, 1)]] StructuredBuffer<Material> bMaterialBuffer;

FSOutput main(FSInput input)
{
    FSOutput output;

    output.vAlbedo = float4(F_Sample(tAlbedo, input.vUV).rgb, 1.0);

#ifdef USE_NORMAL_MAPS
    float2 roughness_metallic = F_Sample(tMetallicRoughness, input.vUV).gb;
    float3 normal_ts = F_Sample(tNormalMap, input.vUV).rgb * 2.0 - 1.0;

    float3x3 TBN = float3x3(input.vTangentWS, input.vBitangentWS, input.vNormalWS);

    float3 normal_ws = mul(normal_ts, TBN);

    // XYZ=Normal, W=Roughness
    output.vNormal = float4(normalize(normal_ws), roughness_metallic.x);
    // Metalness
    output.vAlbedo.w = roughness_metallic.y;
#else
    output.vNormal = float4(input.vNormalWS, 0.5);
#endif

    return output;
}
