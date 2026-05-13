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
    uint4 vJointIndices : ATTR0;
    float4 vJointWeights : ATTR1;
#endif
};

struct VSOutput
{
    float4 vPosition : SV_POSITION;
};


struct VSPushConsts
{
    float4x4 mCameraMatrix;
    uint uiObjectIndex;
};


[[vk::push_constant]] VSPushConsts VSConst;

F_REFLECT(FR_STRUCTBUFFER, 0, 0);
[[vk::binding(0, 0)]] StructuredBuffer<Object> bObjectBuffer;


VSOutput main(VSInput input) : SV_POSITION
{
    VSOutput output;
    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mModel;
    float4x4 MVP = mul(VSConst.mCameraMatrix, model_matrix);
    output.vPosition = mul(MVP, float4(input.vPosition, 1.0));
    return output;
}

///////////////////////////////////
// Pixel Shader
///////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSInput
{
    float4 vPosition: SV_POSITION;
};

void main(FSInput input)
{
}
