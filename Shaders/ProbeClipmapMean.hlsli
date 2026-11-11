#ifndef PROBE_CLIPMAP_MEAN_HLSLI
#define PROBE_CLIPMAP_MEAN_HLSLI

#include "ProbeClipmapForward.hlsli"

// The light around a point in the open, for fog and particles. Needs tIrradiance (declared the F_Texture2D way),
// bProbes (StructuredBuffer<uint4>) and bClipmapForward (StructuredBuffer<ClipmapForwardData>), declared by the
// including shader.

float3 ClipmapMeanSampleIrradiance(uint level, uint slot, float3 direction)
{
	return F_SampleLevel(tIrradiance, ClipmapAtlasUV(level, slot, CLIPMAP_IRRADIANCE_SIZE, direction), 0.0).rgb;
}

/**
 * The mean radiance around a point: the eight probes around it, without a visibility test, each the average of its
 * up and down irradiance. The fallback light outside of the clipmap.
 */
float3 ClipmapMeanRadiance(float3 position_ws)
{
	const ClipmapScene scene = bClipmapForward[0].Clipmap;
	const float3 half_extent = float3(CLIPMAP_GRID_X, CLIPMAP_GRID_Y, CLIPMAP_GRID_Z) * 0.5 - 2.0;

	uint level = CLIPMAP_LEVELS - 1;
	bool found = false;

	for (uint index = CLIPMAP_LEVELS; index > 0; index--) {
		const ClipmapLevel clip = scene.Levels[index - 1];
		const bool inside = all(abs(position_ws * clip.vParams.y - clip.vCenter.xyz) < half_extent);

		level = select(inside, index - 1, level);
		found = or(found, inside);
	}

	float3 sum = float3(0.0, 0.0, 0.0);
	float total = 0.0;

	// Where none of the eight probes is filled (all in walls or outside the map), the next coarser level is read
	for (uint attempt = 0; attempt < CLIPMAP_LEVELS; attempt++) {
		const float3 local = position_ws * scene.Levels[level].vParams.y;
		const float3 base = floor(local);
		const float3 cell_frac = local - base;

		sum = float3(0.0, 0.0, 0.0);
		total = 0.0;

		[unroll]
		for (uint corner = 0; corner < 8; corner++) {
			const int3 offset = int3(corner, corner >> 1, corner >> 2) & 1;
			const int3 probe = int3(base) + offset;
			const uint slot = ClipmapSlot(probe);

			const float3 axis = lerp(1.0 - cell_frac, cell_frac, float3(offset));
			const bool is_valid = ClipmapDecodeProbe(bProbes[ClipmapProbeIndex(level, slot)], probe).bReady;
			const float weight = select(is_valid, axis.x * axis.y * axis.z, 0.0);

			const float3 mean = (ClipmapMeanSampleIrradiance(level, slot, float3(0.0, 1.0, 0.0)) +
								 ClipmapMeanSampleIrradiance(level, slot, float3(0.0, -1.0, 0.0))) * 0.5;

			sum += mean * weight;
			total += weight;
		}

		if (total > 1e-3 || level + 1 >= CLIPMAP_LEVELS) {
			break;
		}

		level++;
	}

	return select(and(found, total > 1e-6), sum / max(total, 1e-6), bClipmapForward[0].vFallback.rgb);
}

#endif
