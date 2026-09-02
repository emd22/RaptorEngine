
#include "./Helper.hlsl"


//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct TextInstanceData
{
	float2 vPosition;
	float2 vSize;
	float2 vUvMin;
	float2 vUvMax;
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

	float2 corner = input.vPosition.xy * 0.5f + 0.5f;

	float2 text_position = instance.vPosition + (corner * instance.vSize);

	float4 position = float4(text_position, 0.5f, 1.0f);
	output.vPosition = mul(VSConst.mCombinedMatrix, position);

	output.vUV = float2(lerp(instance.vUvMin.x, instance.vUvMax.x, corner.x),
						lerp(instance.vUvMax.y, instance.vUvMin.y, corner.y));

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

F_Texture2D(tFont, 1, 0)

FSOutput main(FSInput input)
{
	FSOutput output;

	float4 sampled = F_Sample(tFont, input.vUV);
	output.vAlbedo = float4(sampled.rgb * input.vTextColor.rgb, sampled.a * input.vTextColor.a);

	return output;
}
