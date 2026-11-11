#ifndef PROBE_CLIPMAP_MEAN_HLSLI
#define PROBE_CLIPMAP_MEAN_HLSLI

#include "ProbeClipmapForward.hlsli"

// The light around a point in the open, for fog and particles. Needs tIrradiance0-2 (declared the F_Texture2D way),
// bState (StructuredBuffer<uint>) and bClipmapForward (StructuredBuffer<ClipmapForwardData>), declared by the
// including shader.

float3 ClipmapMeanSampleIrradiance(uint level, uint slot, float3 direction)
{
	const float2 image_size = float2(CLIPMAP_TILE_COLUMNS, CLIPMAP_GRID_Z) * (CLIPMAP_IRRADIANCE_SIZE + 2);
	const float2 texel = float2(ClipmapTileOrigin(slot, CLIPMAP_IRRADIANCE_SIZE + 2)) + 1.0 +
						 (ClipmapDirectionToOct(direction) * 0.5 + 0.5) * float(CLIPMAP_IRRADIANCE_SIZE);
	const float2 uv = texel / image_size;

	// One level is read per point, so this branches: selecting would read all three images
	if (level == 0) {
		return F_SampleLevel(tIrradiance0, uv, 0.0).rgb;
	}

	if (level == 1) {
		return F_SampleLevel(tIrradiance1, uv, 0.0).rgb;
	}

	return F_SampleLevel(tIrradiance2, uv, 0.0).rgb;
}

/**
 * The mean radiance around a point: the eight probes around it, without a visibility test, each the average of its
 * up and down irradiance. The fallback light outside of the clipmap.
 */
float3 ClipmapMeanRadiance(float3 position_ws)
{
	const ClipmapScene scene = bClipmapForward[0].Clipmap;
	const float3 last = float3(CLIPMAP_GRID_X - 1, CLIPMAP_GRID_Y - 1, CLIPMAP_GRID_Z - 1);

	uint level = CLIPMAP_LEVELS - 1;
	bool found = false;

	for (uint index = CLIPMAP_LEVELS; index > 0; index--) {
		const float3 local = position_ws * scene.Levels[index - 1].vParams.y - float3(scene.Levels[index - 1].vOrigin.xyz);
		const bool inside = and(all(local >= 0.0), all(local < last));

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
			const uint slot = ClipmapSlot(int3(base) + offset);

			const float3 axis = lerp(1.0 - cell_frac, cell_frac, float3(offset));
			const bool is_valid = HAS_FLAG(bState[ClipmapProbeIndex(level, slot)], CLIPMAP_STATE_VALID);
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
