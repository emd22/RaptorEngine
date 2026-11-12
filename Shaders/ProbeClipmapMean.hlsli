#ifndef PROBE_CLIPMAP_MEAN_HLSLI
#define PROBE_CLIPMAP_MEAN_HLSLI

#include "ProbeClipmapForward.hlsli"

// The light around a point in the open, for fog and particles. Needs tClipmapMean (declared the F_Texture3D way, with
// a linear sampler that repeats) and bClipmapForward (StructuredBuffer<ClipmapForwardData>), declared by the including
// shader.
//
// tClipmapMean holds a texel per probe, written by the blend (ProbeClipmapBlend.hlsl): its mean light (the average
// of its irradiance straight up and straight down) times whether it can be read yet, and that in A. The levels are
// stacked in Z. X and Y wrap as the slots do, so the sampler's filter across them is the probes' trilinear weight;
// Z is filtered by hand, since the levels share that axis.

float4 ClipmapMeanSampleLevel(uint level, float3 local)
{
	const float3 base = floor(local);
	const int z = int(base.z);
	const float inv_depth = 1.0 / float(CLIPMAP_GRID_Z * CLIPMAP_LEVELS);
	const float level_z = float(level * CLIPMAP_GRID_Z) + 0.5;

	const float2 uv = (local.xy + 0.5) / float2(CLIPMAP_GRID_X, CLIPMAP_GRID_Y);
	const float z0 = (float(z & (CLIPMAP_GRID_Z - 1)) + level_z) * inv_depth;
	const float z1 = (float((z + 1) & (CLIPMAP_GRID_Z - 1)) + level_z) * inv_depth;

	const float4 near = F_SampleLevel(tClipmapMean, float3(uv, z0), 0.0);
	const float4 far = F_SampleLevel(tClipmapMean, float3(uv, z1), 0.0);

	return lerp(near, far, local.z - base.z);
}

/**
 * The mean radiance around a point: the eight probes around it that can be read, without a visibility test, each the
 * average of its up and down irradiance. The fallback light outside of the clipmap.
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

	float4 mean = float4(0.0, 0.0, 0.0, 0.0);

	// Where none of the eight probes can be read (all in walls or outside the map), the next coarser level is
	for (uint attempt = 0; attempt < CLIPMAP_LEVELS; attempt++) {
		mean = ClipmapMeanSampleLevel(level, position_ws * scene.Levels[level].vParams.y);

		if (or(mean.a > 1e-3, level + 1 >= CLIPMAP_LEVELS)) {
			break;
		}

		level++;
	}

	return select(and(found, mean.a > 1e-6), mean.rgb / max(mean.a, 1e-6), bClipmapForward[0].vFallback.rgb);
}

#endif
