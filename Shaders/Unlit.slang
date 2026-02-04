
#include "./Object.slang.inc"

//////////////////////////////////
// Vertex shader
//////////////////////////////////

struct VSOut
{
    float4 vPosition : SV_POSITION;
    float2 vUV       : TEXCOORD0;
};

struct VSIn
{
    float3 vPosition : POSITION;
    float3 vNormal : NORMAL;
    float2 vUV : TEXCOORD0;
    float3 vTangent : TANGENT;
    uint uiInstanceId : SV_InstanceID;
};

struct VSPushConsts
{
    float4x4 mViewProjection;
	uint uiObjectIndex;
    uint uiMaterialIndex;
};

layout(set = 2, binding = 0) StructuredBuffer<Object> bObjectBuffer;

[[vk::push_constant]] VSPushConsts VSConst;

[shader("vertex")]

VSOut VertexMain(VSIn in)
{
    VSOut out;

    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + in.uiInstanceId].mModel;

    float4x4 MVP = mul(VSConst.mViewProjection, model_matrix);

    out.vPosition = mul(MVP, float4(in.vPosition, 1.0));
    out.vUV = in.vUV;

    return out;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

#include "./MaterialProperties.slang.inc"

struct FSInput
{
    float4 vPosition: SV_POSITION;
    float2 vUV : TEXCOORD0;
};

struct FSOutput {
    float4 vAlbedo : SV_TARGET0;
};

layout(set = 0, binding = 0) Sampler2D sAlbedo;

layout(set = 1, binding = 0) StructuredBuffer<Material> bMaterialBuffer;

[shader("fragment")]

FSOutput FragmentMain(FSInput in)
{
    FSOutput out;

    float4 material_color = unpackUnorm4x8ToFloat(bMaterialBuffer[VSConst.uiMaterialIndex].uiBaseColor);
    out.vAlbedo = float4(sAlbedo.Sample(in.vUV).rgb, 1.0) + material_color;

    return out;
}
