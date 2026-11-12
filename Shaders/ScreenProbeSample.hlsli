#ifndef SCREEN_PROBE_SAMPLE_HLSLI
#define SCREEN_PROBE_SAMPLE_HLSLI

#include "ScreenProbeCommon.hlsli"

// Reads the screen probes around a pixel, as ScreenProbeFilter.hlsl leaves them. Needs bScreenProbes
// (StructuredBuffer<float4>) declared by the including shader.

/// Below this much probe weight a pixel takes the clipmap's light, blending over to it from SCREEN_PROBE_FULL_WEIGHT
#define SCREEN_PROBE_MIN_WEIGHT 0.05
#define SCREEN_PROBE_FULL_WEIGHT 0.2
/// 0: the 2x2 probes around the pixel, bilinear, which on the filtered probes is a 4x4 tent. 1: 4x4, a tent twice as
/// wide.
#ifndef SCREEN_PROBE_FILTER_RADIUS
#define SCREEN_PROBE_FILTER_RADIUS 0
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
		const uint index = uint(tile.x + tile.y * tiles.x) * SCREEN_PROBE_FILTERED_STRIDE;

		const float4 plane = bScreenProbes[index];
		const uint4 sh_rg = asuint(bScreenProbes[index + 1]);
		const uint2 sh_b_bits = asuint(bScreenProbes[index + 2]).xy;

		const float2 tent_distance = abs(float2(offset) - frac_xy) / float(SCREEN_PROBE_FILTER_RADIUS + 1);
		const float2 axis = saturate(1.0 - tent_distance);
		const float plane_distance = abs(dot(surface, plane.xyz) - plane.w);
		const float plane_weight = saturate(1.0 - plane_distance / (surface_distance * SCREEN_PROBE_PLANE_SHARE + 0.01));
		const float normal_weight = GIPow8(saturate(dot(geometric_normal, plane.xyz)));
		const float4 sh_r = ScreenProbeUnpackHalf4(sh_rg.xy);
		const float4 sh_g = ScreenProbeUnpackHalf4(sh_rg.zw);
		const float4 sh_b = ScreenProbeUnpackHalf4(sh_b_bits);
		const float3 irradiance = float3(ScreenProbeIrradiance(sh_r, normal), ScreenProbeIrradiance(sh_g, normal),
										 ScreenProbeIrradiance(sh_b, normal));

		// ScreenProbeTrace only writes finite probes (a broken one as unplaced, with zero coefficients), so nothing
		// here can be NaN. An unplaced probe's plane is zero, so is its normal weight.
		const float weight = axis.x * axis.y * plane_weight * normal_weight;

		sum += irradiance * weight;
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
