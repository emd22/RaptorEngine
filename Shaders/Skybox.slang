module Skybox;

/////////////////////////////////
// Vertex shader
/////////////////////////////////

struct VSOut
{
    float4 vPosition : SV_POSITION;
    float3 vUVW : TEXCOORD0;
};

struct VSIn
{
    float3 vPosition : ATTR0;
};


struct VSPushConsts
{
    float4x4 mProjectionMatrix;
};

[[vk::push_constant]] VSPushConsts VSConst;

[shader("vertex")]

VSOut VertexMain(VSIn in)
{
    VSOut out;

    out.vUVW = in.vPosition;
    out.vUVW.x *= -1.0f;

    out.vPosition = mul(float4(in.vPosition, 1.0), VSConst.mProjectionMatrix);

    return out;
}

/////////////////////////////////
// Fragment shader
/////////////////////////////////

struct FSOut
{
    float4 vAlbedo : SV_TARGET0;
};


struct FSIn
{
    float3 vUVW : TEXCOORD0;
};

layout(set = 0, binding = 0) SamplerCube sSky;

[shader("fragment")]

FSOut FragmentMain(FSIn in)
{
    FSOut out;
    out.vAlbedo = sSky.Sample(in.vUVW);
    return out;
}
