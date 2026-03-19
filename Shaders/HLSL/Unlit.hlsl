#include "./Helper.hlsl"


//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct VSOutput
{
    float4 vPosition : SV_POSITION;
    float2 vUV       : TEXCOORD0;
};

struct VSInput
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

VSOutput main(VSInput input)
{
    VSOutput output;

    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mModel;
    float4x4 MVP = mul(VSConst.mViewProjection, model_matrix);

    output.vPosition = mul(MVP, float4(input.vPosition, 1.0));
    output.vUV = input.vUV;

    return output;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSInput
{
    float4 vPosition: SV_POSITION;
    float2 vUV : TEXCOORD0;
};

struct FSOutput {
    float4 vAlbedo : SV_TARGET0;
};

F_TEXTURE2D(tAlbedo, 0)
F_TEXTURE2D(tNormalMap, 1)
F_TEXTURE2D(tMetallicRoughness, 2)

layout(set = 1, binding = 0) StructuredBuffer<Material> bMaterialBuffer;

FSOutput main(FSInput input)
{
    FSOutput output;

    float4 material_color = F_UnpackUintToFloat4(bMaterialBuffer[VSConst.uiMaterialIndex].uiBaseColor);
    output.vAlbedo = float4(sAlbedo.Sample(input.vUV).rgb, 1.0) + material_color;

    return output;
}
