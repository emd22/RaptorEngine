#ifndef BRICK_TRACE_HLSLI
#define BRICK_TRACE_HLSLI

#include "BrickCommon.hlsli"

// Needs bBrickIndirection (StructuredBuffer<uint>), tBrickAtlas (F_Texture3D) and BRICK_SCENE, the BrickScene in the
// shader's constants, declared by the including shader. The scene is read from the constants in place: a copy of it
// indexed by level would live in slow per-thread memory.

#define BRICK_MAX_STEPS 256
/// A ray hits once it is closer than this many texels of its level, since an unsigned field never reaches zero
/// between its texels
#define BRICK_HIT_TEXELS 0.75
#define BRICK_EXIT_EPSILON 0.02

struct BrickHit
{
	float fDistance;
	uint uiSteps;
	uint uiLevel;
	bool bHit;
};

/**
 * The finest level from `min_level` up whose window holds `position` and whose brick there is not still waiting for its
 * distances
 */
bool BrickFind(float3 position, uint min_level, out uint out_level, out int3 out_coord, out uint out_atlas)
{
	out_level = 0;
	out_coord = int3(0, 0, 0);
	out_atlas = BRICK_EMPTY;

	[loop]
	for (uint level = min_level; level < BRICK_SCENE.vInfo.x; level++) {
		const int3 coord = int3(floor(position * BRICK_SCENE.Levels[level].vParams.y));

		if (!BrickInWindow(BRICK_SCENE.Levels[level], coord)) {
			continue;
		}

		const uint atlas = bBrickIndirection[BrickSlotIndex(level, coord)];

		if (atlas != BRICK_PENDING) {
			out_level = level;
			out_coord = coord;
			out_atlas = atlas;
			return true;
		}
	}

	return false;
}

float BrickSampleDistance(uint level, int3 coord, uint atlas, float3 position)
{
	const BrickLevel brick_level = BRICK_SCENE.Levels[level];

	float3 local = (position - float3(coord) * brick_level.vParams.x) * brick_level.vParams.y * BRICK_TEXELS;
	local = clamp(local, 0.5, BRICK_TEXELS - 0.5);

	const float3 uvw = (BrickAtlasOrigin(atlas) + local) * BRICK_SCENE.vInvAtlasSize.xyz;

	return F_SampleLevel(tBrickAtlas, uvw, 0).r * brick_level.vParams.z;
}

/// Distance to the nearest surface, up to the margin of the level the point is in
float BrickDistance(float3 position)
{
	uint level;
	int3 coord;
	uint atlas;

	const bool found = BrickFind(position, 0, level, coord, atlas);
	const bool is_empty = or(!found, atlas == BRICK_EMPTY);

	const float margin = BRICK_SCENE.Levels[level].vParams.z;
	const float distance = BrickSampleDistance(level, coord, select(is_empty, 0u, atlas), position);

	return select(is_empty, margin, distance);
}

float3 BrickNormal(float3 position, float texel)
{
	const float2 h = float2(texel, 0.0);

	const float3 gradient = float3(BrickDistance(position + h.xyy) - BrickDistance(position - h.xyy),
								   BrickDistance(position + h.yxy) - BrickDistance(position - h.yxy),
								   BrickDistance(position + h.yyx) - BrickDistance(position - h.yyx));

	return gradient * rsqrt(max(dot(gradient, gradient), 1e-12));
}

/// The texel size of the level that holds `position`, or of the coarsest level if none does
float BrickTexelAt(float3 position)
{
	uint level;
	int3 coord;
	uint atlas;

	const bool found = BrickFind(position, 0, level, coord, atlas);

	return BRICK_SCENE.Levels[select(found, level, BRICK_SCENE.vInfo.x - 1)].vParams.w;
}

/// The level whose texels are as wide as a cone of `cone_slope` (its width over its length) is at `distance`
uint BrickConeLevel(float distance, float cone_slope)
{
	const float texels = distance * cone_slope / BRICK_SCENE.Levels[0].vParams.w;
	const float level = floor(log2(max(texels, 1.0)));

	return uint(min(level, float(BRICK_SCENE.vInfo.x - 1)));
}

/**
 * Sphere traces the bricks. A non-zero `cone_slope` moves to coarser levels as the ray gets longer, so a ray stands in
 * for a cone of that width: surfaces get thicker and steps longer with distance.
 */
BrickHit BrickTrace(float3 origin, float3 direction, float max_distance, float cone_slope)
{
	BrickHit hit;
	hit.fDistance = max_distance;
	hit.uiSteps = 0;
	hit.uiLevel = 0;
	hit.bHit = false;

	const float3 safe_direction = select(abs(direction) < 1e-6, select(direction < 0.0, -1e-6, 1e-6), direction);
	const float3 inv_direction = 1.0 / safe_direction;
	const bool3 is_positive = direction >= 0.0;

	float t = 0.0;

	// The brick the last step was in. Sphere steps are at most half a brick, so most of them stay in it and can skip
	// the indirection load.
	uint level = BRICK_MAX_LEVELS;
	int3 coord = int3(0, 0, 0);
	uint atlas = BRICK_EMPTY;

	[loop]
	for (uint step = 0; step < BRICK_MAX_STEPS; step++) {
		hit.uiSteps = step + 1;

		const float3 position = origin + direction * t;
		const uint min_level = BrickConeLevel(t, cone_slope);

		bool is_same_brick = false;

		if (and(level >= min_level, level < BRICK_MAX_LEVELS)) {
			const int3 same_level_coord = int3(floor(position * BRICK_SCENE.Levels[level].vParams.y));
			is_same_brick = all(same_level_coord == coord);
		}

		if (!is_same_brick) {
			if (!BrickFind(position, min_level, level, coord, atlas)) {
				break;
			}
		}

		const BrickLevel brick_level = BRICK_SCENE.Levels[level];
		const bool is_empty = atlas == BRICK_EMPTY;

		// An empty brick has nothing within its margin, so every finer brick inside it is empty too. Skip the
		// coarsest empty brick around the point.
		uint skip_level = level;
		int3 skip_coord = coord;

		if (is_empty) {
			[loop]
			for (uint up = level + 1; up < BRICK_SCENE.vInfo.x; up++) {
				const int3 up_coord = int3(floor(position * BRICK_SCENE.Levels[up].vParams.y));

				if (!BrickInWindow(BRICK_SCENE.Levels[up], up_coord)) {
					break;
				}

				if (bBrickIndirection[BrickSlotIndex(up, up_coord)] != BRICK_EMPTY) {
					break;
				}

				skip_level = up;
				skip_coord = up_coord;
			}
		}

		const float skip_size = BRICK_SCENE.Levels[skip_level].vParams.x;
		const float3 brick_min = float3(skip_coord) * skip_size;
		const float3 brick_max = brick_min + skip_size;
		const float3 to_exit = (select(is_positive, brick_max, brick_min) - position) * inv_direction;
		const float exit_distance = max(min(to_exit.x, min(to_exit.y, to_exit.z)), 0.0) +
									brick_level.vParams.w * BRICK_EXIT_EPSILON;

		const float distance = BrickSampleDistance(level, coord, select(is_empty, 0u, atlas), position);

		if (and(!is_empty, distance < brick_level.vParams.w * BRICK_HIT_TEXELS)) {
			hit.fDistance = t;
			hit.uiLevel = level;
			hit.bHit = true;
			break;
		}

		t += select(is_empty, exit_distance, distance);

		// Written so that a NaN distance stops the ray too
		if (!(t < max_distance)) {
			break;
		}
	}

	return hit;
}

#endif
