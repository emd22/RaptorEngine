#ifndef SCREEN_PROBE_SAMPLE_HLSLI
#define SCREEN_PROBE_SAMPLE_HLSLI

#include "ScreenProbeCommon.hlsli"

// Reads the screen probes around a pixel. Needs bScreenProbes (StructuredBuffer<float4>) declared by the including
// shader.

/// A screen probe stands for a pixel within this share of the pixel's distance from the eye of its plane
#define SCREEN_PROBE_PLANE_SHARE 0.02
/// Below this much probe weight a pixel takes the clipmap's light, blending over to it from SCREEN_PROBE_FULL_WEIGHT
#define SCREEN_PROBE_MIN_WEIGHT 0.05
#define SCREEN_PROBE_FULL_WEIGHT 0.2
/// 0: the 2x2 probes around the pixel, bilinear. 1: 4x4, a tent twice as wide, which also filters between probes.
#ifndef SCREEN_PROBE_FILTER_RADIUS
#define SCREEN_PROBE_FILTER_RADIUS 1
#endif

/**
 * Irradiance / pi along `normal` at the surface seen at full resolution `pixel`, from the screen probes of the tiles
 * around it. `out_weight` is how much of it the probes cover: nothing where they all sit on other surfaces.
 */
float3 ScreenProbeGather(float3 surface, float surface_distance, float3 geometric_normal, float3 normal, uint2 pixel,
						 uint2 screen_size, uint tile_size, out float out_weight)
{
	const int2 tiles = int2((screen_size + tile_size - 1) / tile_size);
	const float2 tile_position = (float2(pixel) + 0.5) / float(tile_size) - 0.5;
	const int2 base = int2(floor(tile_position));
	const float2 frac_xy = tile_position - float2(base);
	const int span = 2 + 2 * SCREEN_PROBE_FILTER_RADIUS;

	float3 sum = float3(0.0, 0.0, 0.0);
	float total = 0.0;

	for (uint corner = 0; corner < uint(span * span); corner++) {
		const int2 offset = int2(int(corner) % span, int(corner) / span) - SCREEN_PROBE_FILTER_RADIUS;
		const int2 tile = clamp(base + offset, int2(0, 0), tiles - 1);
		const uint index = uint(tile.x + tile.y * tiles.x) * SCREEN_PROBE_STRIDE;

		const float4 probe_position = bScreenProbes[index];
		const float3 probe_normal = bScreenProbes[index + 1].xyz;

		const float2 tent_distance = abs(float2(offset) - frac_xy) / float(SCREEN_PROBE_FILTER_RADIUS + 1);
		const float2 axis = saturate(1.0 - tent_distance);
		const float plane = abs(dot(surface - probe_position.xyz, probe_normal));
		const float plane_weight = saturate(1.0 - plane / (surface_distance * SCREEN_PROBE_PLANE_SHARE + 0.01));
		const float normal_weight = pow(saturate(dot(geometric_normal, probe_normal)), 8.0);
		const float4 sh_r = bScreenProbes[index + 2];
		const float4 sh_g = bScreenProbes[index + 3];
		const float4 sh_b = bScreenProbes[index + 4];
		const float3 irradiance = float3(ScreenProbeIrradiance(sh_r, normal), ScreenProbeIrradiance(sh_g, normal),
										 ScreenProbeIrradiance(sh_b, normal));

		// The coefficients are checked, not the irradiance: max(NaN, 0) is 0 on some GPUs, which would pass a broken
		// probe as one with a black channel. Selected, not multiplied away: 0 * NaN is NaN.
		const bool is_finite = and(and(GIIsFinite(sh_r.xyz), GIIsFinite(sh_g.xyz)),
								   and(GIIsFinite(sh_b.xyz), GIIsFinite(float3(sh_r.w, sh_g.w, sh_b.w))));
		const bool is_usable = and(and(probe_position.w > 0.0, GIIsFinite(probe_position.xyz)), is_finite);
		const float weight = select(is_usable, axis.x * axis.y * plane_weight * normal_weight, 0.0);

		sum += select(weight > 0.0, irradiance * weight, float3(0.0, 0.0, 0.0));
		total += weight;
	}

	out_weight = total;
	return sum / max(total, 1e-6);
}

/// How much of a pixel's light comes from the screen probes rather than the clipmap, from ScreenProbeGather's weight
float ScreenProbeShare(float weight)
{
	return saturate((weight - SCREEN_PROBE_MIN_WEIGHT) / (SCREEN_PROBE_FULL_WEIGHT - SCREEN_PROBE_MIN_WEIGHT));
}

#endif
