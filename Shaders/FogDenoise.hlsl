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
	float4 vHistory : SV_TARGET1;
	float vHistoryDepth : SV_TARGET2;
};

F_Texture2D(tFog, 0, 0);
F_Texture2D(tDepth, 1, 0);

struct FogResolveConsts
{
	float2 vDepthParams;
};

F_FrameData(FogResolveConsts, Consts, 1);

#define FOG_DENOISE_DEPTH_SHARPNESS 2000.0

float LinearViewDepth(float raw_depth)
{
	return Consts.vDepthParams.y / max(raw_depth - Consts.vDepthParams.x, 1e-12);
}

float FogTexelInvDepth(int2 fog_pixel, int2 fog_max, int2 depth_size)
{
	const int2 clamped = clamp(fog_pixel, int2(0, 0), fog_max);
	const int2 depth_pixel = min(clamped * 2 + 1, depth_size - 1);

	return rcp(LinearViewDepth(F_SampleLoad(tDepth, int3(depth_pixel, 0)).r));
}

float SmallerSlope(float backward, float forward)
{
	return select(abs(backward) < abs(forward), backward, forward);
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

	const int2 center = int2(input.vPosition.xy);
	const float center_inv = FogTexelInvDepth(center, fog_max, depth_size);

	const float slope_x = SmallerSlope(center_inv - FogTexelInvDepth(center - int2(1, 0), fog_max, depth_size),
									   FogTexelInvDepth(center + int2(1, 0), fog_max, depth_size) - center_inv);
	const float slope_y = SmallerSlope(center_inv - FogTexelInvDepth(center - int2(0, 1), fog_max, depth_size),
									   FogTexelInvDepth(center + int2(0, 1), fog_max, depth_size) - center_inv);

	float4 sum = float4(0.0, 0.0, 0.0, 0.0);
	float total_weight = 0.0;

	[unroll]
	for (int y = -2; y < 2; y++) {
		[unroll]
		for (int x = -2; x < 2; x++) {
			const int2 tap = clamp(center + int2(x, y), int2(0, 0), fog_max);

			const float predicted = center_inv + (slope_x * float(x)) + (slope_y * float(y));
			const float relative = (FogTexelInvDepth(tap, fog_max, depth_size) - predicted) / center_inv;
			const float weight = rcp(1.0 + (relative * relative * FOG_DENOISE_DEPTH_SHARPNESS));

			sum += F_SampleLoad(tFog, int3(tap, 0)) * weight;
			total_weight += weight;
		}
	}

	output.vFog = sum / total_weight;
	output.vHistory = F_SampleLoad(tFog, int3(center, 0));
	output.vHistoryDepth = rcp(center_inv);

	return output;
}
