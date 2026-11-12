#ifndef PROBE_CLIPMAP_SAMPLE_HLSLI
#define PROBE_CLIPMAP_SAMPLE_HLSLI

#include "ProbeClipmapCommon.hlsli"

// Reads the probe clipmap. Needs tIrradiance and tMoments (declared the F_Texture2D way), bProbes
// (StructuredBuffer<uint4>) and CLIPMAP_SCENE, the ClipmapScene in the shader's constants, declared by the including
// shader.

/// Least standard deviation of a probe's depth, in metres
#define CLIPMAP_MIN_DEPTH_DEVIATION 0.05
#define CLIPMAP_MIN_WEIGHT 1e-6
/// Probe weights below this are squashed towards zero, so barely visible probes (behind a wall) don't leak
#define CLIPMAP_WEIGHT_CRUSH 0.05

/// Receivers are moved this much of a probe spacing along their normal and towards the viewer before the visibility
/// test, so a surface doesn't fall behind its own depth (after DDGI, Majercik et al. 2019)
#define CLIPMAP_NORMAL_BIAS 0.05
#define CLIPMAP_VIEW_BIAS 0.2

/// Over this many probes before a level's usable window ends, pixels blend over to the next coarser level
#define CLIPMAP_BLEND_PROBES 2.0

/// A cell whose filled probes have less than this much of the trilinear weight is read from the next coarser level
#define CLIPMAP_MIN_VALID 1e-3

/// A level whose probe weights around a receiver sum to less than this (before normalizing) only keeps that share of it,
/// and hands the rest to the next coarser level. Normalized, a few nearly zero weights (a crease, a probe or two that
/// barely see the surface) give whatever the least hidden probe holds: black slivers, bright glows.
#define CLIPMAP_CONFIDENT_WEIGHT 0.02

/// Probes behind the receiver's surface get this little of their weight
#define CLIPMAP_BEHIND_WEIGHT 1e-3

struct ClipmapGatherResult
{
	/// Irradiance / pi along the diffuse normal and along the reflection
	float3 vDiffuse;
	float3 vSpecular;
	/// The finest level that was read, CLIPMAP_LEVELS where none holds the surface
	uint uiLevel;
	/// Trilinear weight of the filled probes around the surface
	float fValid;
	/// Mean visibility weight of the filled probes, 1 where nothing occludes them
	float fVisibility;
	/// Sum of the probe weights before they are normalized. Near 1 where the probes around the surface face it and see
	/// it, near 0 where the gather has almost nothing to go on.
	float fWeight;
};

float3 ClipmapSampleIrradiance(uint level, uint slot, float3 direction)
{
	return F_SampleLevel(tIrradiance, ClipmapAtlasUV(level, slot, CLIPMAP_IRRADIANCE_SIZE, direction), 0.0).rgb;
}

/// The mean distance and the mean squared distance. Bilinear filtering them keeps the spread between texels in the
/// variance, which softens the visibility at occluder edges.
float2 ClipmapSampleMoments(uint level, uint slot, float3 direction)
{
	return F_SampleLevel(tMoments, ClipmapAtlasUV(level, slot, CLIPMAP_MOMENTS_SIZE, direction), 0.0).rg;
}

/// Chebyshev's bound on the visibility of a receiver `distance` away, with a variance floor of the angle one depth
/// texel covers
float ClipmapChebyshev(float distance, float2 moments)
{
	// tan(sqrt(pi) / size), half the angle covered by an average texel
	const float texel_slope = 0.11166;

	const float mean = moments.x;
	const float floor_deviation = max(mean * texel_slope, CLIPMAP_MIN_DEPTH_DEVIATION);
	const float variance = max(moments.y - mean * mean, floor_deviation * floor_deviation);

	const float d = distance - mean;

	return select(distance <= mean, 1.0, variance / (variance + d * d));
}

/**
 * Weight of a probe by where it is against the receiver's surface: full in front of it, falling to almost nothing a
 * little way behind its plane. A probe back there is inside or past whatever the surface belongs to, and the depth
 * test alone lets it through thin walls. The receiver is inside its probe cell, so some probe is always in front.
 */
float ClipmapFacingWeight(float3 position, float3 normal, float3 probe_position)
{
	const float3 to_probe = probe_position - position;
	const float len = length(to_probe);
	const float facing = saturate(dot(to_probe / max(len, 1e-4), normal) * 2.0 + 0.5);

	return select(len < 1e-4, 1.0, facing * facing + CLIPMAP_BEHIND_WEIGHT);
}

ClipmapProbe ClipmapLoadProbe(uint level, int3 probe)
{
	return ClipmapDecodeProbe(bProbes[ClipmapProbeIndex(level, ClipmapSlot(probe))], probe);
}

/**
 * How far inside level `level`'s usable window `position` is, in probes. Every corner of the probe cell around a point
 * is in the window while this is positive. It is measured from the window's centre, which follows the camera smoothly,
 * so the hand over band moves with the camera instead of jumping a probe at a time.
 */
float ClipmapWindowEdge(uint level, float3 position, float3 bias)
{
	const ClipmapLevel clip = CLIPMAP_SCENE.Levels[level];
	const float3 half_extent = float3(CLIPMAP_GRID_X, CLIPMAP_GRID_Y, CLIPMAP_GRID_Z) * 0.5 - 2.0;
	const float3 to_edge = half_extent - abs(position * clip.vParams.y + bias - clip.vCenter.xyz);

	return min(to_edge.x, min(to_edge.y, to_edge.z));
}

/**
 * The finest level whose window holds the probe cell around `position`, and `out_coarser_share`, how much of the next
 * coarser level is blended in near the window's edge. `bias` is how far the receiver is moved before it is looked up,
 * in probe spacings, which is the same on every level.
 */
uint ClipmapFindLevel(float3 position, float3 bias, out bool found, out float out_coarser_share)
{
	found = false;
	uint chosen = CLIPMAP_LEVELS - 1;
	float edge = 0.0;

	for (uint level = CLIPMAP_LEVELS; level > 0; level--) {
		const float level_edge = ClipmapWindowEdge(level - 1, position, bias);
		const bool inside = level_edge > 0.0;

		chosen = select(inside, level - 1, chosen);
		edge = select(inside, level_edge, edge);
		found = or(found, inside);
	}

	out_coarser_share = select(chosen + 1 < CLIPMAP_LEVELS, saturate(1.0 - edge / CLIPMAP_BLEND_PROBES), 0.0);

	return chosen;
}

/// The gather from the eight probes of `level` around a receiver. `with_specular` is a constant at every call, so the
/// reads along `reflected` fold away where it is false.
ClipmapGatherResult ClipmapGatherLevel(uint level, float3 position, float3 bias, float3 geometric_normal,
									   float3 diffuse_normal, float3 reflected, const bool with_specular)
{
	const ClipmapLevel clip = CLIPMAP_SCENE.Levels[level];

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
		const ClipmapProbe data = ClipmapDecodeProbe(bProbes[ClipmapProbeIndex(level, slot)], probe);

		const float3 axis = lerp(1.0 - cell_frac, cell_frac, float3(offset));
		const float valid_trilinear = select(data.bReady, axis.x * axis.y * axis.z, 0.0);
		valid += valid_trilinear;

		const float3 probe_position = ClipmapProbePosition(clip, probe) + data.vOffset;
		const float3 to_receiver = biased - probe_position;
		const float distance = length(to_receiver);
		const float3 direction = to_receiver / max(distance, 1e-4);

		const float2 depth = ClipmapSampleMoments(level, slot, direction);
		const float visibility = select(distance < 1e-4, 1.0, ClipmapChebyshev(distance, depth));
		visibility_sum += visibility * valid_trilinear;

		// Visibility weighs in linearly. Cubed, as DDGI has it, a receiver a little past a probe's mean depth (most of
		// a flat wall, seen from the probes half a cell in front of it) lost nearly all of that probe's weight, so
		// each part of the wall took the light of whichever probe happened to see it best, in blotches.
		float weight = max(ClipmapFacingWeight(position, geometric_normal, probe_position) * visibility, CLIPMAP_MIN_WEIGHT);
		weight = select(weight < CLIPMAP_WEIGHT_CRUSH, weight * (weight * weight / (CLIPMAP_WEIGHT_CRUSH * CLIPMAP_WEIGHT_CRUSH)),
						weight);
		weight *= select(data.bActive, 1.0, CLIPMAP_INACTIVE_WEIGHT);
		weight *= valid_trilinear;

		diffuse += ClipmapSampleIrradiance(level, slot, diffuse_normal) * weight;

		if (with_specular) {
			specular += ClipmapSampleIrradiance(level, slot, reflected) * weight;
		}

		total += weight;
	}

	// Crushed weights of occluded probes are far below 1e-6, so the floor has to sit under them: a larger one stops
	// normalizing wherever every probe is occluded, and the surface goes black
	const float inv_total = 1.0 / max(total, 1e-30);

	ClipmapGatherResult result;
	result.vDiffuse = diffuse * inv_total;
	result.vSpecular = specular * inv_total;
	result.uiLevel = level;
	result.fValid = valid;
	result.fVisibility = visibility_sum / max(valid, 1e-6);
	result.fWeight = total;

	return result;
}

ClipmapGatherResult ClipmapGatherAt(float3 position, float3 geometric_normal, float3 view_direction, float3 diffuse_normal,
									float3 reflected, const bool with_specular)
{
	const float3 bias = geometric_normal * CLIPMAP_NORMAL_BIAS + view_direction * CLIPMAP_VIEW_BIAS;

	bool found;
	float coarser_share;
	const uint first_level = ClipmapFindLevel(position, bias, found, coarser_share);

	ClipmapGatherResult result;
	result.vDiffuse = float3(0.0, 0.0, 0.0);
	result.vSpecular = float3(0.0, 0.0, 0.0);
	result.uiLevel = CLIPMAP_LEVELS;
	result.fValid = 0.0;
	result.fVisibility = 0.0;
	result.fWeight = 0.0;

	// The first level with filled probes keeps all but the coarser share near its window's edge (and less where its
	// probes hardly see the surface), and the next one with filled probes takes what is left. A cell with none filled
	// (all in walls or outside the map, or not traced yet) has nothing to give, so it is passed over. Most pixels read
	// one level, the ones in a band or a crease two or three.
	float taken = 0.0;
	float keep = 1.0 - coarser_share;

	[loop]
	for (uint level = first_level; level < CLIPMAP_LEVELS; level++) {
		const ClipmapGatherResult gather = ClipmapGatherLevel(level, position, bias, geometric_normal, diffuse_normal,
															  reflected, with_specular);

		const bool usable = gather.fValid > CLIPMAP_MIN_VALID;
		// The coarsest level is held to its confidence too, so where every level is unsure they are mixed by how sure
		// each one is. The floor keeps a usable level from giving nothing at all.
		const float confidence = max(saturate(gather.fWeight / CLIPMAP_CONFIDENT_WEIGHT), 1e-3);
		const float share = select(usable, select(level + 1 < CLIPMAP_LEVELS, keep, 1.0) * confidence * (1.0 - taken), 0.0);

		result.vDiffuse += gather.vDiffuse * share;
		result.vSpecular += gather.vSpecular * share;
		result.fValid += gather.fValid * share;
		result.fVisibility += gather.fVisibility * share;
		result.fWeight += gather.fWeight * share;
		result.uiLevel = select(and(usable, result.uiLevel == CLIPMAP_LEVELS), level, result.uiLevel);

		taken += share;
		keep = select(usable, 1.0, keep);

		if (taken > 0.999) {
			break;
		}
	}

	const float inv_taken = 1.0 / max(taken, 1e-6);

	result.vDiffuse *= inv_taken;
	result.vSpecular *= inv_taken;
	result.fValid *= inv_taken;
	result.fWeight *= inv_taken;
	result.fVisibility = select(taken > 0.0, result.fVisibility * inv_taken, 1.0);
	result.uiLevel = select(found, result.uiLevel, CLIPMAP_LEVELS);

	return result;
}

/**
 * Irradiance / pi along `diffuse_normal` and along `reflected` at a surface, from the eight probes around it, the
 * weights worked out once. `geometric_normal` and `view_direction` (unit, from the surface towards the viewer) move the
 * receiver off its surface, and `geometric_normal` weights the probes behind it down.
 */
ClipmapGatherResult ClipmapGather(float3 position, float3 geometric_normal, float3 view_direction, float3 diffuse_normal,
								  float3 reflected)
{
	return ClipmapGatherAt(position, geometric_normal, view_direction, diffuse_normal, reflected, true);
}

/// ClipmapGather() without the specular term, which is left black
ClipmapGatherResult ClipmapGatherDiffuse(float3 position, float3 normal, float3 view_direction)
{
	return ClipmapGatherAt(position, normal, view_direction, normal, normal, false);
}

/**
 * The light that reaches a probe ray's hit before bouncing off it: irradiance / pi along `normal` from the eight probes
 * of the finest level around `position` (or the next one where none of them is filled), without their depth test and
 * without blending levels. The full gather is most of a ray's cost; this one reads a third as much. The facing weight
 * still drops the probes behind the hit's surface, which are the ones a wall would otherwise let through. Returns a
 * negative value where no level holds the point. `view_direction` points back towards the probe that traced the ray.
 */
float3 ClipmapGatherBounce(float3 position, float3 normal, float3 view_direction)
{
	const float3 bias = normal * CLIPMAP_NORMAL_BIAS + view_direction * CLIPMAP_VIEW_BIAS;

	bool found;
	float coarser_share;
	uint level = ClipmapFindLevel(position, bias, found, coarser_share);

	float3 diffuse = float3(0.0, 0.0, 0.0);
	float total = 0.0;

	[loop]
	for (; level < CLIPMAP_LEVELS; level++) {
		const ClipmapLevel clip = CLIPMAP_SCENE.Levels[level];
		const float3 local = (position + bias * clip.vParams.x) * clip.vParams.y;
		const float3 base = floor(local);
		const float3 cell_frac = local - base;

		diffuse = float3(0.0, 0.0, 0.0);
		total = 0.0;

		for (uint corner = 0; corner < 8; corner++) {
			const int3 offset = int3(corner, corner >> 1, corner >> 2) & 1;
			const int3 probe = int3(base) + offset;
			const uint slot = ClipmapSlot(probe);
			const ClipmapProbe data = ClipmapDecodeProbe(bProbes[ClipmapProbeIndex(level, slot)], probe);

			const float3 axis = lerp(1.0 - cell_frac, cell_frac, float3(offset));
			const float3 probe_position = ClipmapProbePosition(clip, probe) + data.vOffset;

			float weight = axis.x * axis.y * axis.z * ClipmapFacingWeight(position, normal, probe_position);
			weight *= select(data.bActive, 1.0, CLIPMAP_INACTIVE_WEIGHT);
			weight = select(data.bReady, weight, 0.0);

			diffuse += ClipmapSampleIrradiance(level, slot, normal) * weight;
			total += weight;
		}

		if (total > CLIPMAP_MIN_VALID * 1e-2) {
			break;
		}
	}

	return select(and(found, total > 0.0), diffuse / max(total, 1e-30), float3(-1.0, -1.0, -1.0));
}

#endif
