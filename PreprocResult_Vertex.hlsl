
#include "./Helper.hlsl"








#line 11

struct VSInput
{
    float3 vPosition : POSITION;
    float3 vNormal : NORMAL;
    float2 vUV : TEXCOORD0;
    float3 vTangent : TANGENT;
    uint uiInstanceId : SV_InstanceID;
};

struct VSOutput
{
    float4 vPosition : SV_POSITION;
    float3 vNormalWS   : NORMAL;
    float2 vUV       : TEXCOORD0;
#line 30
    float3 vTangentWS : TANGENT;
    float3 vBitangentWS : BITANGENT;
    
};

struct VSPushConsts
{
    float4x4 mViewProjection;
	uint uiObjectIndex;
    uint uiMaterialIndex;
};



[[vk::binding(0, 2)]] StructuredBuffer<Object> bObjectBuffer;

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
    VSOutput output;

    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mModel;

    float4x4 MVP = mul(VSConst.mViewProjection, model_matrix);

#line 73

    output.vPosition = mul(MVP, float4(input.vPosition, 1.0));
    output.vNormalWS = normalize(mul((float3x3)model_matrix, input.vNormal));
    // output.vDebugColor = float4(1.0, 1.0, 1.0, 1.0);

#line 79
    output.vTangentWS = normalize(mul((float3x3)model_matrix, input.vTangent));
    output.vBitangentWS = cross(output.vNormalWS, output.vTangentWS);

    output.vUV = input.vUV;

    return output;
}





