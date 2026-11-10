#ifndef PROBE_CLIPMAP_SAMPLE_HLSLI
#define PROBE_CLIPMAP_SAMPLE_HLSLI

#include "ProbeCommon.hlsli"
#include "ProbeClipmapCommon.hlsli"

// Reads the probe clipmap. Needs tIrradiance0-2 and tMoments0-2 (declared the F_Texture2D way), bState
// (StructuredBuffer<uint>), bProbeData (StructuredBuffer<float4>) and CLIPMAP_SCENE, the ClipmapScene in the shader's
// constants, declared by the including shader.

/// Receivers are moved this much of a probe spacing along their normal and towards the viewer before the visibility
/// test, so a surface doesn't fall behind its own depth (after DDGI, Majercik et al. 2019)
#define CLIPMAP_NORMAL_BIAS 0.05
#define CLIPMAP_VIEW_BIAS 0.2

/// Over this many probes before a level's window ends, pixels dither over to the next coarser level
#define CLIPMAP_BLEND_PROBES 2.0

struct ClipmapGatherResult
{
	/// Irradiance / pi along the diffuse normal and along the reflection
	float3 vDiffuse;
	float3 vSpecular;
	/// The level that was read, CLIPMAP_LEVELS where none holds the surface
	uint uiLevel;
	/// Trilinear weight of the filled probes around the surface
	float fValid;
	/// Mean visibility weight of the filled probes, 1 where nothing occludes them
	float fVisibility;
};

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

/// The mean distance and the mean squared distance. Bilinear filtering them keeps the spread between texels in the
/// variance, which softens the visibility at occluder edges.
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

/// Chebyshev's bound on the visibility of a receiver `distance` away, with a variance floor of the angle one depth
/// texel of the level covers
float ClipmapChebyshev(uint level, float distance, float2 moments)
{
	// tan(sqrt(pi) / size), half the angle covered by an average texel
	const float texel_slope = select(level == 0, 0.11166, 0.22528);

	const float mean = moments.x;
	const float floor_deviation = max(mean * texel_slope, PROBE_MIN_DEPTH_STDDEV);
	const float variance = max(moments.y - mean * mean, floor_deviation * floor_deviation);

	const float d = distance - mean;

	return select(distance <= mean, 1.0, variance / (variance + d * d));
}

bool ClipmapIsValid(uint level, uint slot) { return HAS_FLAG(bState[level * CLIPMAP_PROBES_PER_LEVEL + slot], CLIPMAP_STATE_VALID); }

bool ClipmapIsActive(float4 probe_data) { return HAS_FLAG(asuint(probe_data.w), CLIPMAP_PROBE_ACTIVE); }

/**
 * The finest level whose window holds every corner of the probe cell around `position`, or the next coarser one where
 * `dither` (in [0, 1)) falls inside the band at the window's edge
 */
uint ClipmapFindLevel(float3 position, float dither, out bool found)
{
	const float3 last = float3(CLIPMAP_GRID_X - 1, CLIPMAP_GRID_Y - 1, CLIPMAP_GRID_Z - 1);

	found = false;
	uint chosen = CLIPMAP_LEVELS - 1;
	float edge = 0.0;

	for (uint level = CLIPMAP_LEVELS; level > 0; level--) {
		const ClipmapLevel clip = CLIPMAP_SCENE.Levels[level - 1];
		const float3 local = position * clip.vParams.y - float3(clip.vOrigin.xyz);
		const bool inside = and(all(local >= 0.0), all(local < last));

		const float3 to_edge = min(local, last - local);
		const float level_edge = min(to_edge.x, min(to_edge.y, to_edge.z));

		chosen = select(inside, level - 1, chosen);
		edge = select(inside, level_edge, edge);
		found = or(found, inside);
	}

	const float coarser_share = saturate(1.0 - edge / CLIPMAP_BLEND_PROBES);
	const bool use_coarser = and(chosen + 1 < CLIPMAP_LEVELS, dither < coarser_share);

	return select(use_coarser, chosen + 1, chosen);
}

/**
 * Irradiance / pi along `diffuse_normal` and along `reflected` at a surface, from the eight probes around it, the
 * weights worked out once. `geometric_normal` and `view_direction` (unit, from the surface towards the viewer) move the
 * receiver off its surface, and `geometric_normal` weights the probes behind it down. `dither` in [0, 1) picks between
 * two levels near a window's edge.
 */
ClipmapGatherResult ClipmapGather(float3 position, float3 geometric_normal, float3 view_direction, float3 diffuse_normal,
								  float3 reflected, float dither)
{
	bool found;
	const uint level = ClipmapFindLevel(position, dither, found);
	const ClipmapLevel clip = CLIPMAP_SCENE.Levels[level];

	const float3 bias = geometric_normal * CLIPMAP_NORMAL_BIAS + view_direction * CLIPMAP_VIEW_BIAS;
	const float3 biased = position + bias * clip.vParams.x;
	const float3 local = biased * clip.vParams.y;
	const float3 base = floor(local);
	const float3 cell_frac = local - base;

	float3 diffuse = float3(0.0, 0.0, 0.0);
	float3 specular = float3(0.0, 0.0, 0.0);
	float total = 0.0;
	float valid = 0.0;
	float visibility_sum = 0.0;

	for (uint corner = 0; corner < 8; corner++) {
		const int3 offset = int3(corner, corner >> 1, corner >> 2) & 1;
		const int3 probe = int3(base) + offset;
		const uint slot = ClipmapSlot(probe);

		const float3 axis = lerp(1.0 - cell_frac, cell_frac, float3(offset));
		const float trilinear = axis.x * axis.y * axis.z;

		const bool is_valid = ClipmapIsValid(level, slot);
		const float valid_trilinear = select(is_valid, trilinear, 0.0);
		valid += valid_trilinear;

		const float4 probe_data = bProbeData[ClipmapProbeIndex(level, slot)];
		const float3 probe_position = ClipmapProbePosition(clip, probe) + probe_data.xyz;
		const float3 to_receiver = biased - probe_position;
		const float distance = length(to_receiver);
		const float3 direction = to_receiver / max(distance, 1e-4);

		const float2 moments = ClipmapSampleMoments(level, slot, direction);
		const float visibility = select(distance < 1e-4, 1.0, ClipmapChebyshev(level, distance, moments));
		visibility_sum += visibility * valid_trilinear;

		float weight = max(ProbeBackfaceWeight(position, geometric_normal, probe_position) * visibility * visibility *
							   visibility,
						   PROBE_MIN_WEIGHT);
		weight = select(weight < PROBE_WEIGHT_CRUSH, weight * (weight * weight / (PROBE_WEIGHT_CRUSH * PROBE_WEIGHT_CRUSH)),
						weight);
		weight *= select(ClipmapIsActive(probe_data), 1.0, CLIPMAP_INACTIVE_WEIGHT);
		weight *= valid_trilinear;

		diffuse += ClipmapSampleIrradiance(level, slot, diffuse_normal) * weight;
		specular += ClipmapSampleIrradiance(level, slot, reflected) * weight;
		total += weight;
	}

	const float inv_total = 1.0 / max(total, 1e-6);

	ClipmapGatherResult result;
	result.vDiffuse = diffuse * inv_total;
	result.vSpecular = specular * inv_total;
	result.uiLevel = select(found, level, CLIPMAP_LEVELS);
	result.fValid = valid;
	result.fVisibility = visibility_sum / max(valid, 1e-6);

	return result;
}

/// Interleaved gradient noise (Jimenez 2014), for dithering by screen position
float ClipmapScreenDither(float2 pixel) { return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715)))); }

#endif
