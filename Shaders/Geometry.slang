module Geometry;


struct VSOut
{
    float4 vPosition : SV_Position;
    float3 vNormal   : NORMAL;
    float2 vUV       : TEXCOORD0;
};

struct VSIn
{
    float3 vPosition : ATTR0;
    float3 vNormal : ATTR1;
    float2 vUV : ATTR2;
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

    out.vNormal = mul(in.vNormal, VSConst.mNormalMatrix);
    out.vUV = in.vUV;

    return out;
}


#include "./MaterialProperties.slang.inc"

struct FSOut {
    float4 vAlbedo : SV_Target0;
    float4 vNormal : SV_Target1;

};

struct FSIn
{
    float3 vNormal : NORMAL;
    float2 vUV : TEXCOORD0;
};

layout(set = 0, binding = 0) Sampler2D sAlbedo;
layout(set = 1, binding = 0) StructuredBuffer<Material> bMaterialBuffer;

[shader("fragment")]

FSOut FragmentMain(FSIn in)
{
    FSOut out;

    float4 material_color = unpackUnorm4x8ToFloat(bMaterialBuffer[VSConst.uiMaterialIndex].uiBaseColor);
    out.vAlbedo = float4(sAlbedo.Sample(in.vUV).rgb, 1.0) + material_color;
    out.vNormal = float4(in.vNormal, 1.0f);

    return out;
}
