#include "./Helper.hlsl"


//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct VSOutput
{
    float4 vPosition : SV_POSITION;
    float2 vUV       : TEXCOORD0;
#ifdef IS_DEBUG_LAYER
    float4 vDebugColor : ATTR0;
#endif
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
#ifdef IS_DEBUG_LAYER
    float4x4 mCombinedMatrix;
    uint uiDebugColor;
#else
    float4x4 mCameraMatrix;
	uint uiObjectIndex;
    uint uiMaterialIndex;
#endif
};

#ifdef IS_DEBUG_LAYER
// Pass
#else
[[vk::binding(0, 2)]] StructuredBuffer<Object> bObjectBuffer;
#endif // !IS_DEBUG_LAYER

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
    VSOutput output;

#ifdef IS_DEBUG_LAYER
    float4x4 MVP = VSConst.mCombinedMatrix;
#else
    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mModel;
    float4x4 MVP = mul(VSConst.mCameraMatrix, model_matrix);
#endif

    output.vPosition = mul(MVP, float4(input.vPosition, 1.0));
    output.vUV = input.vUV;

#ifdef IS_DEBUG_LAYER
    output.vDebugColor = F_UnpackUIntToFloat4(VSConst.uiDebugColor);
#endif

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
#ifdef IS_DEBUG_LAYER
    float4 vDebugColor : ATTR0;
#endif
};

struct FSOutput {
    float4 vAlbedo : SV_TARGET0;
};

#ifdef IS_DEBUG_LAYER
// Pass
#else

F_Texture2D(tAlbedo, 0)
F_Texture2D(tNormalMap, 1)
F_Texture2D(tMetallicRoughness, 2)

[[vk::binding(0, 1)]] StructuredBuffer<Material> bMaterialBuffer;

#endif // !IS_DEBUG_LAYER

FSOutput main(FSInput input)
{
    FSOutput output;

#ifdef IS_DEBUG_LAYER
    output.vAlbedo = float4(input.vDebugColor.rgb, 1.0);
#else
    output.vAlbedo = float4(F_Sample(tAlbedo, input.vUV).rgb, 1.0);
#endif

    return output;
}
