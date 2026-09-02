
#include "./Helper.hlsl"


//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct TextInstanceData
{
	float2 vPosition; // Top-left corner in pixel space
	float2 vSize;	  // Glyph size in pixel space
	float2 vUvMin;	  // Atlas UV of the top-left of the glyph
	float2 vUvMax;	  // Atlas UV of the bottom-right of the glyph
};

struct VSInput
{
	float3 vPosition   : POSITION;
	float3 vNormal     : NORMAL;
	float2 vUV         : TEXCOORD0;
	float3 vTangent    : TANGENT;
	uint uiInstanceId  : SV_InstanceID;
};

struct VSOutput
{
	float4 vPosition : SV_POSITION;
	float2 vUV       : TEXCOORD0;
	float4 vTextColor : COLOR;
};

struct VSPushConsts
{
	float4x4 mCombinedMatrix;
	uint uiTextColor;
	float fAtlasMinU;
	float fAtlasMinV;
	float fAtlasMaxU;
	float fAtlasMaxV;
};

F_StructBuffer(bTextInstances, TextInstanceData, 0, 0);

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
	VSOutput output;

	TextInstanceData instance = bTextInstances[input.uiInstanceId];

	// The unit quad spans [-1, 1], remap it to a [0, 1] corner basis so the per-instance
	// position is the top-left corner of the glyph quad.
	float2 corner = input.vPosition.xy * 0.5f + 0.5f;

	float2 text_position = instance.vPosition + (corner * instance.vSize);

	float4 position = float4(text_position, 0.5f, 1.0f);
	output.vPosition = mul(VSConst.mCombinedMatrix, position);

	output.vUV = lerp(instance.vUvMin, instance.vUvMax, corner);

	output.vTextColor = F_UnpackUIntToFloat4(VSConst.uiTextColor);

	return output;
}

//////////////////////////////////
// Pixel shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSInput
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
	float4 vTextColor : COLOR;
};

struct FSOutput
{
	float4 vAlbedo : SV_TARGET0;
};

F_Texture2D(tFont, 0, 1)

FSOutput main(FSInput input)
{
	FSOutput output;

	float4 sampled = F_Sample(tFont, input.vUV);
	output.vAlbedo = float4(sampled.rgb * input.vTextColor.rgb, sampled.a * input.vTextColor.a);

	return output;
}
