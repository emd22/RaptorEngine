#include "./Helper.hlsl"


//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)


struct VSInput
{
    float3 vPosition : POSITION;
#ifdef IS_DEBUG_LAYER
    // Skip
#else
    float3 vNormal : NORMAL;
    float2 vUV : TEXCOORD0;
    float3 vTangent : TANGENT;
    uint uiInstanceId : SV_InstanceID;
#endif
};

struct VSOutput
{
    float4 vPosition : SV_POSITION;
#ifdef IS_DEBUG_LAYER
    float4 vDebugColor : ATTR0;
#else
    float2 vUV       : TEXCOORD0;

#ifdef IS_TEXT
    float4 vTextColor : ATTR0;
#endif // IS_TEXT

#endif // IS_DEBUG_LAYER
};


struct VSPushConsts
{
#ifdef IS_DEBUG_LAYER
    float4x4 mCombinedMatrix;
    uint uiDebugColor;
#else
#ifdef IS_TEXT
    float4x4 mCombinedMatrix;
    uint uiTextColor;
#else
    float4x4 mCameraMatrix;
	uint uiObjectIndex;
    uint uiMaterialIndex;
#endif // IS_TEXT
#endif
};

#ifdef IS_DEBUG_LAYER
// Pass
#else
#ifdef IS_TEXT
// Pass
#else
F_StructBuffer(bObjectBuffer, Object, 0, 2);
#endif // IS_TEXT
#endif // IS_DEBUG_LAYER

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
    VSOutput output;

#ifdef IS_DEBUG_LAYER
    float4x4 MVP = VSConst.mCombinedMatrix;
#else
#ifdef IS_TEXT
    float4x4 MVP = VSConst.mCombinedMatrix;
#else
    float4x4 model_matrix = bObjectBuffer[VSConst.uiObjectIndex + input.uiInstanceId].mModel;
    float4x4 MVP = mul(VSConst.mCameraMatrix, model_matrix);
#endif // IS_TEXT
#endif

    output.vPosition = mul(MVP, float4(input.vPosition, 1.0));

#ifdef IS_DEBUG_LAYER
    output.vDebugColor = F_UnpackUIntToFloat4(VSConst.uiDebugColor);
#else
    output.vUV = input.vUV;
#ifdef IS_TEXT
    output.vTextColor = F_UnpackUIntToFloat4(VSConst.uiTextColor);
#endif
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
#ifdef IS_DEBUG_LAYER
    float4 vDebugColor : ATTR0;

#else
    float2 vUV : TEXCOORD0;
#ifdef IS_TEXT
    float4 vTextColor : ATTR0;
#endif
#endif
};

struct FSOutput {
    float4 vAlbedo : SV_TARGET0;
};

#ifdef IS_DEBUG_LAYER
// Pass
#else

#ifdef IS_TEXT

F_Texture2D(tFontAtlas, 0)

#else

F_Texture2D(tAlbedo, 0)
F_Texture2D(tNormalMap, 1)
F_Texture2D(tMetallicRoughness, 2)

F_StructBuffer(bMaterialBuffer, Material, 0, 1);

#endif // IS_TEXT
#endif // IS_DEBUG_LAYER

FSOutput main(FSInput input)
{
    FSOutput output;

#ifdef IS_TEXT
    float4 char_color = F_Sample(tFontAtlas, input.vUV);
    output.vAlbedo = char_color;
#endif

#ifdef IS_DEBUG_LAYER
    output.vAlbedo = float4(input.vDebugColor.rgb, 1.0);
#else
    output.vAlbedo = float4(F_Sample(tAlbedo, input.vUV).rgb, 1.0);
#endif

    return output;
}
