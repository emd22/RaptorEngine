
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
	nointerpolation uint uiIsImage : TEXCOORD1;
};

struct VSPushConsts
{
	float4x4 mCombinedMatrix;
	uint uiTextColor;
	uint uiInstanceBase;
	float fAtlasMinU;
	float fAtlasMinV;
	float fAtlasMaxU;
	float fAtlasMaxV;
	uint uiIsImage;
};

F_StructBuffer(bTextInstances, TextInstanceData, 0, 0);

[[vk::push_constant]] VSPushConsts VSConst;

VSOutput main(VSInput input)
{
	VSOutput output;

	TextInstanceData instance = bTextInstances[input.uiInstanceId + VSConst.uiInstanceBase];

	float2 corner = input.vPosition.xy * 0.5f + 0.5f;

	float2 text_position = instance.vPosition + (corner * instance.vSize);

	float4 position = float4(text_position, 0.5f, 1.0f);
	output.vPosition = mul(position, VSConst.mCombinedMatrix);

	output.vUV = float2(lerp(instance.vUvMin.x, instance.vUvMax.x, corner.x),
						lerp(instance.vUvMax.y, instance.vUvMin.y, corner.y));

	output.vTextColor = F_UnpackUIntToFloat4(VSConst.uiTextColor);
	output.uiIsImage = VSConst.uiIsImage;

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
	nointerpolation uint uiIsImage : TEXCOORD1;
};

struct FSOutput
{
	float4 vAlbedo : SV_TARGET0;
};

F_Texture2D(tFont, 1, 0)

FSOutput main(FSInput input)
{
	FSOutput output;

	// Taken before branching so the derivatives stay defined
	const float2 footprint = float2(ddx(input.vUV.x), ddy(input.vUV.y));

	if (input.uiIsImage != 0) {
		// Images are usually drawn smaller than their source and have no mips, so box filter the pixel's footprint
		// with a 4x4 grid of point taps (exact for 1:1, 2:1 and 4:1). Each tap is premultiplied before averaging, as
		// transparent texels often hold black that would otherwise darken the edges.
		float4 premultiplied = 0.0;

		[unroll]
		for (int y = 0; y < 4; y++) {
			[unroll]
			for (int x = 0; x < 4; x++) {
				const float2 offset = (float2(x, y) - 1.5) * 0.25 * footprint;
				const float4 texel = F_Sample(tFont, input.vUV + offset);
				premultiplied += float4(texel.rgb * texel.a, texel.a);
			}
		}

		premultiplied /= 16.0;

		const float3 color = premultiplied.rgb / max(premultiplied.a, 1e-5);
		output.vAlbedo = float4(color, premultiplied.a) * input.vTextColor;
		return output;
	}

	float4 sampled = F_Sample(tFont, input.vUV);
	output.vAlbedo = lerp(float4(0.1, 0.1, 0.1, 0.6), float4(input.vTextColor.rgb, sampled.a * input.vTextColor.a), sampled.a);

	return output;
}
