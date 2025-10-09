module Geometry;

struct VSOut
{
    float4 vPosition : SV_Position;
    float3 vNormal   : NORMAL;
    float2 vUV       : TEXCOORD0;
    uint uiMaterialIndex;
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
    float4x4 mNormalMatrix;
    uint uiMaterialIndex;
};

[[vk::push_constant]] VSPushConsts VSConst;

[shader("vertex")]

VSOut VertexMain(VSIn in)
{
    VSOut out;

    out.vPosition = mul(float4(in.vPosition, 1.0), VSConst.mMVP);

    float3x3 normal_matrix = float3x3(VSConst.mNormalMatrix);
    out.vNormal = mul(in.vNormal, normal_matrix);
    out.vUV = in.vUV;
    out.uiMaterialIndex = VSConst.uiMaterialIndex;

    return out;
}
