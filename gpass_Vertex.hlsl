
#include "./Util.hlsl"


///////////////////////////////////
// Vertex Shader
///////////////////////////////////

#line 1


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

//
[[vk::binding(2, 0)]] StructuredBuffer<Object> bObjectBuffer;

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

