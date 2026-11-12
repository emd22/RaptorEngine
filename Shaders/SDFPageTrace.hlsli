#ifndef SDF_PAGE_TRACE_HLSLI
#define SDF_PAGE_TRACE_HLSLI

#include "SDFPageCommon.hlsli"

#define SDF_PAGE_MAX_STEPS 256
/// A ray hits once it is closer than this many texels of its level, since an unsigned field never reaches zero
/// between its texels
#define SDF_PAGE_HIT_TEXELS 0.75
#define SDF_PAGE_EXIT_EPSILON 0.02

struct SDFPageHit
{
	float fDistance;
	uint uiSteps;
	uint uiLevel;
	/// The page the ray stopped in, valid on a hit
	uint uiAtlas;
	int3 vCoord;
	bool bHit;
};

/**
 * The finest level from `min_level` up whose window holds `position` and whose page there is not still waiting for its
 * distances
 */
bool SDFPageFind(float3 position, uint min_level, out uint out_level, out int3 out_coord, out uint out_atlas)
{
	out_level = 0;
	out_coord = int3(0, 0, 0);
	out_atlas = SDF_PAGE_EMPTY;

	[loop]
	for (uint level = min_level; level < SDF_PAGE_SCENE.vInfo.x; level++) {
		const int3 coord = int3(floor(position * SDF_PAGE_SCENE.Levels[level].vParams.y));

		if (!SDFPageInWindow(SDF_PAGE_SCENE.Levels[level], coord)) {
			continue;
		}

		const uint atlas = bSDFPageIndirection[SDFPageSlotIndex(level, coord)];

		if (atlas != SDF_PAGE_PENDING) {
			out_level = level;
			out_coord = coord;
			out_atlas = atlas;
			return true;
		}
	}

	return false;
}

float SDFPageSampleDistance(uint level, int3 coord, uint atlas, float3 position)
{
	const SDFPageLevel sdf_page_level = SDF_PAGE_SCENE.Levels[level];

	float3 local = (position - float3(coord) * sdf_page_level.vParams.x) * sdf_page_level.vParams.y * SDF_PAGE_TEXELS;
	local = clamp(local, 0.5, SDF_PAGE_TEXELS - 0.5);

	const float3 uvw = (SDFPageAtlasOrigin(atlas) + local) * SDF_PAGE_SCENE.vInvAtlasSize.xyz;

	return F_SampleLevel(tSDFPageAtlas, uvw, 0).r * sdf_page_level.vParams.z;
}

/// Distance to the nearest surface, up to the margin of the level the point is in
float SDFPageDistance(float3 position)
{
	uint level;
	int3 coord;
	uint atlas;

	const bool found = SDFPageFind(position, 0, level, coord, atlas);
	const bool is_empty = or(!found, atlas == SDF_PAGE_EMPTY);

	// A branch rather than a select: it skips the atlas sample for an empty page
	float distance = SDF_PAGE_SCENE.Levels[level].vParams.z;

	if (!is_empty) {
		distance = SDFPageSampleDistance(level, coord, atlas, position);
	}

	return distance;
}

float3 SDFPageNormal(float3 position, float texel)
{
	const float2 h = float2(texel, 0.0);

	const float3 gradient = float3(SDFPageDistance(position + h.xyy) - SDFPageDistance(position - h.xyy),
								   SDFPageDistance(position + h.yxy) - SDFPageDistance(position - h.yxy),
								   SDFPageDistance(position + h.yyx) - SDFPageDistance(position - h.yyx));

	return gradient * rsqrt(max(dot(gradient, gradient), 1e-12));
}

/// The texel size of the level that holds `position`, or of the coarsest level if none does
float SDFPageTexelAt(float3 position)
{
	uint level;
	int3 coord;
	uint atlas;

	const bool found = SDFPageFind(position, 0, level, coord, atlas);

	return SDF_PAGE_SCENE.Levels[select(found, level, SDF_PAGE_SCENE.vInfo.x - 1)].vParams.w;
}

/// The level whose texels are as wide as a cone of `cone_slope` (its width over its length) is at `distance`
uint SDFPageConeLevel(float distance, float cone_slope)
{
	const float texels = distance * cone_slope / SDF_PAGE_SCENE.Levels[0].vParams.w;
	const float level = floor(log2(max(texels, 1.0)));

	return uint(min(level, float(SDF_PAGE_SCENE.vInfo.x - 1)));
}

SDFPageHit SDFPageTrace(float3 origin, float3 direction, float max_distance, float cone_slope)
{
	SDFPageHit hit;
	hit.fDistance = max_distance;
	hit.uiSteps = 0;
	hit.uiLevel = 0;
	hit.uiAtlas = SDF_PAGE_EMPTY;
	hit.vCoord = int3(0, 0, 0);
	hit.bHit = false;

	const float3 safe_direction = select(abs(direction) < 1e-6, select(direction < 0.0, -1e-6, 1e-6), direction);
	const float3 inv_direction = 1.0 / safe_direction;
	const bool3 is_positive = direction >= 0.0;

	float t = 0.0;

	// The page the last step was in. Sphere steps are at most half a page, so most of them stay in it and can skip
	// the indirection load.
	uint level = SDF_PAGE_MAX_LEVELS;
	int3 coord = int3(0, 0, 0);
	uint atlas = SDF_PAGE_EMPTY;

	[loop]
	for (uint step = 0; step < SDF_PAGE_MAX_STEPS; step++) {
		hit.uiSteps = step + 1;

		const float3 position = origin + direction * t;
		const uint min_level = SDFPageConeLevel(t, cone_slope);

		bool is_same_sdf_page = false;

		if (and(level >= min_level, level < SDF_PAGE_MAX_LEVELS)) {
			const int3 same_level_coord = int3(floor(position * SDF_PAGE_SCENE.Levels[level].vParams.y));
			is_same_sdf_page = all(same_level_coord == coord);
		}

		if (!is_same_sdf_page) {
			if (!SDFPageFind(position, min_level, level, coord, atlas)) {
				break;
			}
		}

		const SDFPageLevel sdf_page_level = SDF_PAGE_SCENE.Levels[level];
		const bool is_empty = atlas == SDF_PAGE_EMPTY;

		// An empty page has nothing within its margin, so every finer page inside it is empty too. Skip the
		// coarsest empty page around the point.
		uint skip_level = level;
		int3 skip_coord = coord;

		if (is_empty) {
			[loop]
			for (uint up = level + 1; up < SDF_PAGE_SCENE.vInfo.x; up++) {
				const int3 up_coord = int3(floor(position * SDF_PAGE_SCENE.Levels[up].vParams.y));

				if (!SDFPageInWindow(SDF_PAGE_SCENE.Levels[up], up_coord)) {
					break;
				}

				if (bSDFPageIndirection[SDFPageSlotIndex(up, up_coord)] != SDF_PAGE_EMPTY) {
					break;
				}

				skip_level = up;
				skip_coord = up_coord;
			}
		}

		const float skip_size = SDF_PAGE_SCENE.Levels[skip_level].vParams.x;
		const float3 sdf_page_min = float3(skip_coord) * skip_size;
		const float3 sdf_page_max = sdf_page_min + skip_size;
		const float3 to_exit = (select(is_positive, sdf_page_max, sdf_page_min) - position) * inv_direction;
		const float exit_distance = max(min(to_exit.x, min(to_exit.y, to_exit.z)), 0.0) +
									sdf_page_level.vParams.w * SDF_PAGE_EXIT_EPSILON;

		// A branch rather than a select: most steps in open space are in empty pages, and their lanes skip the atlas
		// sample this way, even in a divergent wave
		float distance = exit_distance;

		if (!is_empty) {
			distance = SDFPageSampleDistance(level, coord, atlas, position);
		}

		if (and(!is_empty, distance < sdf_page_level.vParams.w * SDF_PAGE_HIT_TEXELS)) {
			hit.fDistance = t;
			hit.uiLevel = level;
			hit.uiAtlas = atlas;
			hit.vCoord = coord;
			hit.bHit = true;
			break;
		}

		t += distance;

		// Written so that a NaN distance stops the ray too
		if (!(t < max_distance)) {
			break;
		}
	}

	return hit;
}

/// Steps a hit at most this many times on the finest level that holds it
#define SDF_PAGE_REFINE_STEPS 6
/// Hits on this level or finer are close enough to their surface already
#define SDF_PAGE_REFINE_MIN_LEVEL 2

/**
 * Moves a hit made on a coarse level up to the surface on the finest level there. A coarse hit stops up to most of a
 * coarse texel short of the surface, where the nearest triangle of its block (half a metre or more across) is often
 * a wall beside it or the back of the wall it hit, so its normal and facing were wrong. A ray that only grazed a
 * surface finds nothing within two coarse texels and keeps its coarse hit.
 */
SDFPageHit SDFPageRefineHit(SDFPageHit hit, float3 origin, float3 direction, float max_distance)
{
	if (or(!hit.bHit, hit.uiLevel < SDF_PAGE_REFINE_MIN_LEVEL)) {
		return hit;
	}

	const float end = min(hit.fDistance + SDF_PAGE_SCENE.Levels[hit.uiLevel].vParams.w * 2.0, max_distance);
	float t = hit.fDistance;

	[loop]
	for (uint step = 0; step < SDF_PAGE_REFINE_STEPS; step++) {
		const float3 position = origin + direction * t;

		uint level;
		int3 coord;
		uint atlas;

		if (!SDFPageFind(position, 0, level, coord, atlas)) {
			break;
		}

		const SDFPageLevel sdf_page_level = SDF_PAGE_SCENE.Levels[level];

		// Nothing is within an empty page's margin of any point in it
		const bool is_empty = atlas == SDF_PAGE_EMPTY;
		float distance = sdf_page_level.vParams.z;

		if (!is_empty) {
			distance = SDFPageSampleDistance(level, coord, atlas, position);
		}

		if (and(!is_empty, distance < sdf_page_level.vParams.w * SDF_PAGE_HIT_TEXELS)) {
			if (level < hit.uiLevel) {
				hit.fDistance = t;
				hit.uiLevel = level;
				hit.uiAtlas = atlas;
				hit.vCoord = coord;
			}

			break;
		}

		t += distance;

		if (!(t < end)) {
			break;
		}
	}

	return hit;
}

#endif
