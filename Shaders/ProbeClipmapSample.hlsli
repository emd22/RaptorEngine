#ifndef PROBE_CLIPMAP_SAMPLE_HLSLI
#define PROBE_CLIPMAP_SAMPLE_HLSLI

#include "ProbeCommon.hlsli"
#include "ProbeClipmapCommon.hlsli"

// Reads the probe clipmap. Needs tIrradiance0-2 and tMoments0-2 (declared the F_Texture2D way), bState
// (StructuredBuffer<uint>), bProbeData (StructuredBuffer<float4>) and CLIPMAP_SCENE, the ClipmapScene in the shader's constants, declared by the including
// shader.

/// Receivers are pushed this much of a probe spacing off their surface before the visibility test
#define CLIPMAP_NORMAL_BIAS 0.25

float2 ClipmapTileUV(uint slot, uint size, float3 direction, float2 image_size)
{
	const float2 origin = float2(ClipmapTileOrigin(slot, size + 2)) + 1.0;
	const float2 texel = origin + (ClipmapDirectionToOct(direction) * 0.5 + 0.5) * float(size);

	return texel / image_size;
}

float3 ClipmapSampleIrradiance(uint level, uint slot, float3 direction)
{
	const float2 image_size = float2(CLIPMAP_TILE_COLUMNS, CLIPMAP_GRID_Z) * (CLIPMAP_IRRADIANCE_SIZE + 2);
	const float2 uv = ClipmapTileUV(slot, CLIPMAP_IRRADIANCE_SIZE, direction, image_size);

	float3 value = F_SampleLevel(tIrradiance2, uv, 0.0).rgb;
	value = select(level == 1, F_SampleLevel(tIrradiance1, uv, 0.0).rgb, value);
	value = select(level == 0, F_SampleLevel(tIrradiance0, uv, 0.0).rgb, value);

	return value;
}

float2 ClipmapSampleMoments(uint level, uint slot, float3 direction)
{
	const uint size = select(level == 0, CLIPMAP_MOMENTS_SIZE_0, CLIPMAP_MOMENTS_SIZE);
	const float2 image_size = float2(CLIPMAP_TILE_COLUMNS, CLIPMAP_GRID_Z) * float(size + 2);
	const float2 uv = ClipmapTileUV(slot, size, direction, image_size);

	float2 value = F_SampleLevel(tMoments2, uv, 0.0).rg;
	value = select(level == 1, F_SampleLevel(tMoments1, uv, 0.0).rg, value);
	value = select(level == 0, F_SampleLevel(tMoments0, uv, 0.0).rg, value);

	return value;
}

bool ClipmapIsValid(uint level, uint slot) { return HAS_FLAG(bState[level * CLIPMAP_PROBES_PER_LEVEL + slot], CLIPMAP_STATE_VALID); }

/// The finest level whose window holds every corner of the probe cell around `position`
uint ClipmapFindLevel(float3 position, out bool found)
{
	const float3 last = float3(CLIPMAP_GRID_X - 1, CLIPMAP_GRID_Y - 1, CLIPMAP_GRID_Z - 1);

	found = false;
	uint chosen = CLIPMAP_LEVELS - 1;

	for (uint level = CLIPMAP_LEVELS; level > 0; level--) {
		const ClipmapLevel clip = CLIPMAP_SCENE.Levels[level - 1];
		const float3 local = position * clip.vParams.y - float3(clip.vOrigin.xyz);
		const bool inside = and(all(local >= 0.0), all(local < last));

		chosen = select(inside, level - 1, chosen);
		found = or(found, inside);
	}

	return chosen;
}

/// Irradiance / pi at a surface, from the eight probes around it
float3 ClipmapGatherIrradiance(float3 position, float3 normal, out uint out_level, out float out_valid)
{
	bool found;
	const uint level = ClipmapFindLevel(position, found);
	const ClipmapLevel clip = CLIPMAP_SCENE.Levels[level];

	const float3 biased = position + normal * (CLIPMAP_NORMAL_BIAS * clip.vParams.x);
	const float3 local = biased * clip.vParams.y;
	const float3 base = floor(local);
	const float3 cell_frac = local - base;

	float3 irradiance = float3(0.0, 0.0, 0.0);
	float total = 0.0;
	float valid = 0.0;

	for (uint corner = 0; corner < 8; corner++) {
		const int3 offset = int3(corner, corner >> 1, corner >> 2) & 1;
		const int3 probe = int3(base) + offset;
		const uint slot = ClipmapSlot(probe);

		const float3 axis = lerp(1.0 - cell_frac, cell_frac, float3(offset));
		const float trilinear = axis.x * axis.y * axis.z;

		const bool is_valid = ClipmapIsValid(level, slot);
		valid += select(is_valid, trilinear, 0.0);

		const float4 probe_data = bProbeData[ClipmapProbeIndex(level, slot)];
		const float3 probe_position = ClipmapProbePosition(clip, probe) + probe_data.xyz;
		const float3 to_receiver = biased - probe_position;
		const float distance = length(to_receiver);
		const float3 direction = to_receiver / max(distance, 1e-4);

		const float2 moments = ClipmapSampleMoments(level, slot, direction);
		const float visibility = select(distance < 1e-4, 1.0, ProbeDepthChebyshev(distance, moments.x, moments.y * moments.y));

		float weight = max(ProbeBackfaceWeight(position, normal, probe_position) * visibility * visibility * visibility,
						   PROBE_MIN_WEIGHT);
		weight = select(weight < PROBE_WEIGHT_CRUSH, weight * (weight * weight / (PROBE_WEIGHT_CRUSH * PROBE_WEIGHT_CRUSH)),
						weight);
		weight *= select(probe_data.w > 0.5, 1.0, CLIPMAP_INACTIVE_WEIGHT);
		weight *= select(is_valid, trilinear, 0.0);

		irradiance += ClipmapSampleIrradiance(level, slot, normal) * weight;
		total += weight;
	}

	out_level = select(found, level, CLIPMAP_LEVELS);
	out_valid = valid;

	return irradiance / max(total, 1e-6);
}

#endif
