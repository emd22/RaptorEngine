#include "./Helper.hlsl"


//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)


struct VSInput
{
    float3 vPosition : POSITION;
};

struct VSOutput
{
    float4 vPosition : SV_POSITION;
    float4 vDebugColor : COLOR;
};


struct VSPushConsts
{
    float4x4 mCombinedMatrix;
    uint uiDebugColor;
};

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
    VSOutput output;
    float4x4 WVP = VSConst.mCombinedMatrix;

    output.vPosition = mul(WVP, float4(input.vPosition, 1.0));
    output.vDebugColor = F_UnpackUIntToFloat4(VSConst.uiDebugColor);

    return output;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSInput
{
    float4 vPosition: SV_POSITION;
    float4 vDebugColor : ATTR0;
};

struct FSOutput {
    float4 vAlbedo : SV_TARGET0;
};


FSOutput main(FSInput input)
{
    FSOutput output;
    output.vAlbedo = float4(input.vDebugColor.rgb, 1.0);
    return output;
}
