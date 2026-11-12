F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "ProbeClipmapCommon.hlsli"

// Keeps the lit scene at half resolution for the next frame's screen probes, see ScreenProbeTrace.hlsl: the radiance of
// full resolution pixel 2i with the pre-exposure taken back out, and its distance from the eye, which tells the
// reprojection whether a texel is still the surface it looks for. Taken right after the forward pass, before the
// particles and the fog are drawn over the lit target.

F_Texture2D(tLit, 0, 0);
F_Texture2D(tDepth, 1, 0);
[[vk::image_format("rgba16f")]] [[vk::binding(2, 0)]] RWTexture2D<float4> tLitHistory;

struct ScreenProbeLitHistoryConsts
{
	float4x4 mInvViewProjection;
	float4 vEyePosition;
	/// Full resolution width and height, then the history's
	uint4 vSize;
	/// One over the pre-exposure
	float4 vParams;
};

F_FrameData(ScreenProbeLitHistoryConsts, Consts, 1);

/// The viewmodel's depth is squashed into the top of the range, see the viewmodel projection
#define VIEWMODEL_STORED_DEPTH 0.99
/// Largest radiance kept, below the half float maximum
#define LIT_HISTORY_MAX 60000.0

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Consts.vSize.zw)) {
		return;
	}

	const int2 pixel = min(int2(id.xy) * 2, int2(Consts.vSize.xy) - 1);
	const float stored_depth = F_SampleLoad(tDepth, int3(pixel, 0)).r;
	const bool has_surface = and(stored_depth > 0.0, stored_depth < VIEWMODEL_STORED_DEPTH);

	const float2 uv = (float2(pixel) + 0.5) / float2(Consts.vSize.xy);
	const float4 world = mul(float4(uv * 2.0 - 1.0, 1.0 - stored_depth, 1.0), Consts.mInvViewProjection);
	const float distance = length(world.xyz / world.w - Consts.vEyePosition.xyz);
	const float3 radiance = min(F_SampleLoad(tLit, int3(pixel, 0)).rgb * Consts.vParams.x, LIT_HISTORY_MAX);

	// Sky, the viewmodel and anything not finite are kept as no surface, which no reprojection matches
	const bool is_usable = and(has_surface, and(GIIsFinite(radiance), GIIsFinite(distance.xxx)));
	tLitHistory[id.xy] = select(is_usable, float4(radiance, distance), float4(0.0, 0.0, 0.0, -1.0));
}
