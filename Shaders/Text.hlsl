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
    float3 vPosition  : POSITION;
    float3 vNormal    : NORMAL;
    float2 vUV        : TEXCOORD0;
    float3 vTangent   : TANGENT;
    uint uiInstanceId : SV_InstanceID;

};

struct VSPushConsts
{
    float4x4 mCameraMatrix;
	uint uiObjectIndex;
    uint uiMaterialIndex;

};

F_StructBuffer(bObjectBuffer, Object, 0, 2);

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
    VSOutput output;

    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mModel;
    float4x4 MVP = mul(VSConst.mCameraMatrix, model_matrix);

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

F_Texture2D(tAlbedo, 0)
F_Texture2D(tNormalMap, 1)
F_Texture2D(tMetallicRoughness, 2)

F_StructBuffer(bMaterialBuffer, Material, 0, 1);


FSOutput main(FSInput input)
{
    FSOutput output;


    float4 tex_color = F_Sample(tAlbedo, input.vUV);
    output.vAlbedo = float4(tex_color.rgb, 1.0);
    if (tex_color.a < 0.5) {
        discard;
    }

    return output;
}
