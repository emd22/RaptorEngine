#include "./Helper.hlsl"

//////////////////////////////////
// Vertex shader
//////////////////////////////////

F_PROGRAM(FPT_VERTEX)

struct VSInput
{
	int iVertexIndex : SV_VertexID;
};

struct VSOutput
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
};

VSOutput main(VSInput input)
{
	VSOutput output;

	float2 out_uv = float2((input.iVertexIndex << 1) & 2, input.iVertexIndex & 2);

	output.vUV = out_uv;
	output.vPosition = float4(out_uv * 2.0 - 1.0, 0.0, 1.0);

	return output;
}

//////////////////////////////////
// Fragment shader
//////////////////////////////////

F_PROGRAM(FPT_PIXEL)

struct FSInput
{
	float4 vPosition : SV_POSITION;
	float2 vUV : TEXCOORD0;
};

struct FSOutput
{
	float4 vFog : SV_TARGET0;
};

F_Texture2D(tFog, 0, 0);
F_Texture2D(tDepth, 1, 0);

struct FogResolveConsts
{
	float2 vDepthParams;
};

F_FrameData(FogResolveConsts, Consts, 1);

#define FOG_UPSAMPLE_DEPTH_SHARPNESS 10000.0
#define FOG_UPSAMPLE_FLOOR 1e-3

float LinearViewDepth(float raw_depth)
{
	return Consts.vDepthParams.y / max(raw_depth - Consts.vDepthParams.x, 1e-12);
}

float PixelInvDepth(int2 pixel, int2 depth_size)
{
	const int2 clamped = clamp(pixel, int2(0, 0), depth_size - 1);

	return rcp(LinearViewDepth(F_SampleLoad(tDepth, int3(clamped, 0)).r));
}

float SmallerSlope(float backward, float forward)
{
	return select(abs(backward) < abs(forward), backward, forward);
}

float UpsampleWeight(float bilinear, int2 tap, int2 pixel, int2 depth_size, float center_inv, float2 slope)
{
	const int2 depth_pixel = min(tap * 2 + 1, depth_size - 1);
	const float2 offset = float2(depth_pixel - pixel);

	const float predicted = center_inv + dot(slope, offset);
	const float relative = (PixelInvDepth(depth_pixel, depth_size) - predicted) / center_inv;

	return (bilinear + FOG_UPSAMPLE_FLOOR) * rcp(1.0 + (relative * relative * FOG_UPSAMPLE_DEPTH_SHARPNESS));
}

FSOutput main(FSInput input)
{
	FSOutput output;

	uint fog_width;
	uint fog_height;
	F_TextureName(tFog).GetDimensions(fog_width, fog_height);

	uint depth_width;
	uint depth_height;
	F_TextureName(tDepth).GetDimensions(depth_width, depth_height);

	const int2 fog_max = int2(fog_width, fog_height) - 1;
	const int2 depth_size = int2(depth_width, depth_height);

	const int2 pixel = int2(input.vPosition.xy);
	const float center_inv = PixelInvDepth(pixel, depth_size);

	const float2 slope = float2(
		SmallerSlope(center_inv - PixelInvDepth(pixel - int2(1, 0), depth_size),
					 PixelInvDepth(pixel + int2(1, 0), depth_size) - center_inv),
		SmallerSlope(center_inv - PixelInvDepth(pixel - int2(0, 1), depth_size),
					 PixelInvDepth(pixel + int2(0, 1), depth_size) - center_inv));

	const float2 fog_coord = (float2(pixel) - 1.0) * 0.5;
	const float2 base_coord = floor(fog_coord);
	const float2 frac_coord = fog_coord - base_coord;
	const int2 base = int2(base_coord);

	const int2 tap00 = clamp(base, int2(0, 0), fog_max);
	const int2 tap10 = clamp(base + int2(1, 0), int2(0, 0), fog_max);
	const int2 tap01 = clamp(base + int2(0, 1), int2(0, 0), fog_max);
	const int2 tap11 = clamp(base + int2(1, 1), int2(0, 0), fog_max);

	const float w00 = UpsampleWeight((1.0 - frac_coord.x) * (1.0 - frac_coord.y), tap00, pixel, depth_size, center_inv,
									 slope);
	const float w10 = UpsampleWeight(frac_coord.x * (1.0 - frac_coord.y), tap10, pixel, depth_size, center_inv, slope);
	const float w01 = UpsampleWeight((1.0 - frac_coord.x) * frac_coord.y, tap01, pixel, depth_size, center_inv, slope);
	const float w11 = UpsampleWeight(frac_coord.x * frac_coord.y, tap11, pixel, depth_size, center_inv, slope);

	const float4 sum = (F_SampleLoad(tFog, int3(tap00, 0)) * w00) + (F_SampleLoad(tFog, int3(tap10, 0)) * w10) +
					   (F_SampleLoad(tFog, int3(tap01, 0)) * w01) + (F_SampleLoad(tFog, int3(tap11, 0)) * w11);

	output.vFog = sum / max(w00 + w10 + w01 + w11, 1e-12);

	return output;
}
