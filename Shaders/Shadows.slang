module Shadows;

#include "./Object.slang.inc"

//////////////////////////////////
// Vertex shader
//////////////////////////////////

struct VSOut
{
    float4 vPosition : SV_POSITION;
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
    float4x4 mCameraMatrix;
    uint uiObjectIndex;
};

[[vk::push_constant]] VSPushConsts VSConst;

layout(set = 0, binding = 0) StructuredBuffer<Object> bObjectBuffer;

[shader("vertex")]

VSOut VertexMain(VSIn in) : SV_POSITION
{
    VSOut out;
    float4x4 MVP = mul(VSConst.mCameraMatrix, bObjectBuffer[VSConst.uiObjectIndex].mModel);
    out.vPosition = mul(MVP, float4(in.vPosition, 1.0));
    return out;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

struct FSIn
{
    float4 vPosition: SV_POSITION;
};

[shader("fragment")]

void FragmentMain(FSIn in)
{
}
