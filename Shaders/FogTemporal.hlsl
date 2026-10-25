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
F_Texture2D(tHistory, 1, 0);
F_Texture2D(tDepth, 2, 0);
F_Texture2D(tHistoryDepth, 3, 0);

struct FogTemporalConsts
{
	float4x4 mPrevCameraMatrix;
	float4 vRays[4];
	float4 vEyeAndBlend;
	float2 vDepthParams;
	uint uiHistoryValid;
	uint uiPad;
};

F_FrameData(FogTemporalConsts, Consts, 1);

#define FOG_TEMPORAL_CLIP_GAMMA 1.25
#define FOG_TEMPORAL_DEPTH_TOLERANCE 0.1
#define FOG_TEMPORAL_MIN_WEIGHT 0.05

float LinearViewDepth(float raw_depth)
{
	return Consts.vDepthParams.y / max(raw_depth - Consts.vDepthParams.x, 1e-12);
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
	const float4 current = F_SampleLoad(tFog, int3(center, 0));

	float4 moment1 = float4(0.0, 0.0, 0.0, 0.0);
	float4 moment2 = float4(0.0, 0.0, 0.0, 0.0);

	[unroll]
	for (int y = -1; y <= 1; y++) {
		[unroll]
		for (int x = -1; x <= 1; x++) {
			const int2 tap = clamp(center + int2(x, y), int2(0, 0), fog_max);
			const float4 value = F_SampleLoad(tFog, int3(tap, 0));

			moment1 += value;
			moment2 += value * value;
		}
	}

	const float4 mean = moment1 / 9.0;
	const float4 sigma = sqrt(max((moment2 / 9.0) - (mean * mean), 0.0));
	const float4 box_min = mean - (sigma * FOG_TEMPORAL_CLIP_GAMMA);
	const float4 box_max = mean + (sigma * FOG_TEMPORAL_CLIP_GAMMA);

	const int2 depth_pixel = min(center * 2 + 1, depth_size - 1);
	const float view_depth = LinearViewDepth(F_SampleLoad(tDepth, int3(depth_pixel, 0)).r);

	const float2 uv = (float2(depth_pixel) + 0.5) / float2(depth_size);
	const float3 ray_top = lerp(Consts.vRays[0].xyz, Consts.vRays[1].xyz, uv.x);
	const float3 ray_bottom = lerp(Consts.vRays[2].xyz, Consts.vRays[3].xyz, uv.x);
	const float3 position_ws = Consts.vEyeAndBlend.xyz + (lerp(ray_top, ray_bottom, uv.y) * view_depth);

	const float4 prev_clip = mul(float4(position_ws, 1.0), Consts.mPrevCameraMatrix);
	const float2 prev_uv = (prev_clip.xy / max(prev_clip.w, 1e-6)) * 0.5 + 0.5;

	const bool on_screen = and(prev_clip.w > 0.0, all(saturate(prev_uv) == prev_uv));

	const float2 history_coord = (((prev_uv * float2(depth_size)) - 0.5) * 0.5) - 0.5;
	const float2 history_base = floor(history_coord);
	const float2 history_frac = history_coord - history_base;
	const int2 history_texel = int2(history_base);

	float4 history_sum = float4(0.0, 0.0, 0.0, 0.0);
	float history_weight = 0.0;

	[unroll]
	for (int corner = 0; corner < 4; corner++) {
		const int2 offset = int2(corner & 1, corner >> 1);
		const int2 tap = clamp(history_texel + offset, int2(0, 0), fog_max);

		const float2 axis_weights = lerp(1.0 - history_frac, history_frac, float2(offset));
		const float tap_depth = F_SampleLoad(tHistoryDepth, int3(tap, 0)).r;
		const bool same_surface = abs(tap_depth - prev_clip.w) <= (prev_clip.w * FOG_TEMPORAL_DEPTH_TOLERANCE);
		const float weight = axis_weights.x * axis_weights.y * select(same_surface, 1.0, 0.0);

		history_sum += F_SampleLoad(tHistory, int3(tap, 0)) * weight;
		history_weight += weight;
	}

	const bool use_history =
		and(and(Consts.uiHistoryValid != 0, on_screen), history_weight > FOG_TEMPORAL_MIN_WEIGHT);

	const float4 history = clamp(history_sum / max(history_weight, 1e-6), box_min, box_max);

	const float4 blended = lerp(history, current, Consts.vEyeAndBlend.w);

	output.vFog = select(use_history, blended, current);

	return output;
}
