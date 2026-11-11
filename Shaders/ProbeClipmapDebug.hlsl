F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "ProbeClipmapCommon.hlsli"

struct ProbeClipmapDebugConsts
{
	ClipmapScene Clipmap;
	float4x4 mInvViewProjection;
	float4 vEyePosition;
	/// Target width and height, view
	uint4 vInfo;
	/// Pre-exposure
	float4 vParams;
};

F_Texture2D(tDepth, 0, 0);
F_Texture2D(tNormal, 1, 0);
F_Texture2D(tIrradiance0, 2, 0);
F_Texture2D(tIrradiance1, 3, 0);
F_Texture2D(tIrradiance2, 4, 0);
F_Texture2D(tMoments0, 5, 0);
F_Texture2D(tMoments1, 6, 0);
F_Texture2D(tMoments2, 7, 0);
F_StructBuffer(bState, uint, 8, 0);

[[vk::image_format("rgba16f")]] [[vk::binding(9, 0)]] RWTexture2D<float4> tOutput;
F_StructBuffer(bProbeData, float4, 10, 0);

F_FrameData(ProbeClipmapDebugConsts, Consts, 1);

#define CLIPMAP_SCENE Consts.Clipmap
#include "ProbeClipmapSample.hlsli"

/// Mirrors ProbeClipmap::eDebugView
#define VIEW_SPHERES_0 1
#define VIEW_SURFACE 2
#define VIEW_LEVELS 3
#define VIEW_SPHERES_1 4
#define VIEW_SPHERES_2 5
/// Mean visibility of the filled probes around each surface: white sees them all, black none, red has none filled
#define VIEW_VISIBILITY 6

#define VIEWMODEL_STORED_DEPTH 0.99
#define SPHERE_RADIUS 0.15
#define SPHERE_MAX_STEPS 192

float3 WorldFromDepth(float2 uv, float stored_depth)
{
	const float4 ndc = float4(uv * 2.0 - 1.0, 1.0 - stored_depth, 1.0);
	const float4 world = mul(ndc, Consts.mInvViewProjection);

	return world.xyz / world.w;
}

float3 LevelColour(uint level)
{
	float3 colour = float3(0.3, 0.3, 0.3);
	colour = select(level == 2, float3(0.3, 0.4, 0.95), colour);
	colour = select(level == 1, float3(0.3, 0.9, 0.3), colour);
	colour = select(level == 0, float3(0.9, 0.3, 0.3), colour);

	return colour;
}

float3 ShadeSpheres(uint level, float3 eye, float3 direction, float max_distance, float3 background)
{
	const ClipmapLevel clip = Consts.Clipmap.Levels[level];
	const float spacing = clip.vParams.x;
	const float radius = SPHERE_RADIUS * spacing;
	const float step = spacing * 0.25;

	float3 colour = background;
	bool done = false;

	[loop]
	for (uint index = 0; index < SPHERE_MAX_STEPS; index++) {
		const float t = float(index) * step;
		const int3 probe = int3(round((eye + direction * t) * clip.vParams.y));
		const float4 probe_data = bProbeData[ClipmapProbeDataIndex(level, probe)];
		const float3 centre = ClipmapProbePosition(clip, probe) + probe_data.xyz;

		const float3 to_centre = centre - eye;
		const float along = dot(to_centre, direction);
		const float miss_sq = dot(to_centre, to_centre) - along * along;
		const float hit_t = along - sqrt(max(radius * radius - miss_sq, 0.0));

		const int3 window = probe - clip.vOrigin.xyz;
		const bool in_window = and(all(window >= 0), all(window < int3(CLIPMAP_GRID_X, CLIPMAP_GRID_Y, CLIPMAP_GRID_Z)));
		const bool is_hit = and(and(miss_sq < radius * radius, hit_t > 0.0), and(hit_t < max_distance, in_window));

		if (and(is_hit, !done)) {
			const uint slot = ClipmapSlot(probe);
			const float3 normal = normalize(eye + direction * hit_t - centre);

			// Unfilled probes are magenta, switched off ones dark grey
			colour = select(ClipmapIsValid(level, slot), ClipmapSampleIrradiance(level, slot, normal) * Consts.vParams.x,
							float3(1.0, 0.0, 1.0));
			colour = select(and(ClipmapIsValid(level, slot), !ClipmapIsActive(probe_data)), float3(0.05, 0.05, 0.05), colour);
			done = true;
		}

		if (or(done, t > max_distance)) {
			break;
		}
	}

	return colour;
}

[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
	const uint2 pixel = thread.xy;

	if (any(pixel >= Consts.vInfo.xy)) {
		return;
	}

	const uint view = Consts.vInfo.z;
	const float2 uv = (float2(pixel) + 0.5) / float2(Consts.vInfo.xy);

	const float stored_depth = F_SampleLoad(tDepth, int3(pixel, 0)).r;
	const float3 normal = normalize(F_SampleLoad(tNormal, int3(pixel, 0)).xyz);

	const bool has_surface = and(stored_depth > 0.0, stored_depth < VIEWMODEL_STORED_DEPTH);

	const float3 eye = Consts.vEyePosition.xyz;
	const float3 surface = WorldFromDepth(uv, stored_depth);
	const float3 direction = normalize(WorldFromDepth(uv, 0.5) - eye);
	const float surface_distance = select(has_surface, length(surface - eye), 1e4);

	float3 colour = tOutput[pixel].rgb;

	if (or(or(view == VIEW_SURFACE, view == VIEW_LEVELS), view == VIEW_VISIBILITY)) {
		const ClipmapGatherResult gather = ClipmapGather(surface, normal, -direction, normal, normal,
														 ClipmapScreenDither(float2(pixel)));

		float3 surface_colour = gather.vDiffuse * Consts.vParams.x;
		surface_colour = select(view == VIEW_LEVELS, LevelColour(gather.uiLevel) * (0.25 + 0.75 * gather.fValid), surface_colour);
		surface_colour = select(view == VIEW_VISIBILITY,
								select(gather.fValid > 1e-3, gather.fVisibility.xxx, float3(1.0, 0.0, 0.0)), surface_colour);

		colour = select(has_surface, surface_colour, colour);
	}
	else {
		uint level = 0;
		level = select(view == VIEW_SPHERES_1, 1u, level);
		level = select(view == VIEW_SPHERES_2, 2u, level);

		colour = ShadeSpheres(level, eye, direction, surface_distance, colour);
	}

	tOutput[pixel] = float4(colour, 1.0);
}
