F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "LightingCommon.hlsli"
#include "DecalCommon.hlsli"

F_CBuffer(FSLightBuffer, 4, 0)
{
	Light Lights[LIGHT_COUNT];
};

F_RWStructBuffer(bLightGrid, TileLightData, 0, 0);
F_RWStructBuffer(bLightIndexList, uint, 1, 0);

F_StructBuffer(bDecals, Decal, 5, 0);
/// `DECAL_MASK_WORDS` words per tile, one bit per decal in `bDecals`
F_RWStructBuffer(bDecalMasks, uint, 6, 0);

struct CSPushConsts
{
	float4x4 mViewProjection;
	float2 vScreenSize;
	uint uiLightCount;
	uint uiTileColumns;
	/// Light that is swapped out for `uiReplacementSlot`, 0xFFFFFFFF for none. Probe captures use this to give the sun
	/// a shadow map centered on the probe.
	uint uiReplacedLight;
	uint uiReplacementSlot;
	/// Number of decals in `bDecals`
	uint uiDecalCount;
};

[[vk::push_constant]] CSPushConsts CSConst;

#define NUM_THREADS (LIGHT_TILE_SIZE * LIGHT_TILE_SIZE)

groupshared uint sTileLightCount;
groupshared uint sTileLightIndices[MAX_LIGHTS_PER_TILE];
groupshared uint sTileDecalMask[DECAL_MASK_WORDS];

float2 ProjectToScreen(float4 clip_space)
{
	float2 ndc = clip_space.xy / clip_space.w;

	return (ndc * 0.5 + 0.5) * CSConst.vScreenSize;
}

/// Whether the screen space bounds of a decal's box overlap the tile. Boxes that reach behind the camera overlap every
/// tile, as their projected corners no longer bound them.
bool DecalIntersectsTile(Decal decal, float2 tile_min, float2 tile_max)
{
	// Each corner is the center plus or minus every half axis, so only these four need transforming
	const float4 center_clip = mul(float4(decal.vCenter, 1.0), CSConst.mViewProjection);
	const float4 axis_x_clip = mul(float4(decal.vHalfAxisX, 0.0), CSConst.mViewProjection);
	const float4 axis_y_clip = mul(float4(decal.vHalfAxisY, 0.0), CSConst.mViewProjection);
	const float4 axis_z_clip = mul(float4(decal.vHalfAxisZ, 0.0), CSConst.mViewProjection);

	float2 rect_min = float2(1e30, 1e30);
	float2 rect_max = float2(-1e30, -1e30);

	uint corners_behind = 0;

	[unroll]
	for (uint corner = 0; corner < 8; corner++) {
		const float3 signs = float3((corner & 1) ? 1.0 : -1.0, (corner & 2) ? 1.0 : -1.0, (corner & 4) ? 1.0 : -1.0);
		const float4 corner_clip = center_clip + (axis_x_clip * signs.x) + (axis_y_clip * signs.y) +
								   (axis_z_clip * signs.z);

		if (corner_clip.w <= 1e-4) {
			corners_behind++;
			continue;
		}

		const float2 corner_screen = ProjectToScreen(corner_clip);

		rect_min = min(rect_min, corner_screen);
		rect_max = max(rect_max, corner_screen);
	}

	if (corners_behind == 8) {
		return false;
	}

	if (corners_behind > 0) {
		return true;
	}

	return all(rect_max >= tile_min) && all(rect_min <= tile_max);
}

[numthreads(LIGHT_TILE_SIZE, LIGHT_TILE_SIZE, 1)]
void main(uint3 group_id : SV_GroupID, uint3 thread_id : SV_GroupThreadID)
{
	const uint local_index = thread_id.y * LIGHT_TILE_SIZE + thread_id.x;

	if (local_index == 0) {
		sTileLightCount = 0;
	}

	for (uint clear_word = local_index; clear_word < DECAL_MASK_WORDS; clear_word += NUM_THREADS) {
		sTileDecalMask[clear_word] = 0;
	}

	GroupMemoryBarrierWithGroupSync();

	// Tile bounds in screen pixels
	const uint2 tile_min = group_id.xy * LIGHT_TILE_SIZE;
	const uint2 tile_max = min(tile_min + LIGHT_TILE_SIZE, uint2(CSConst.vScreenSize));

	for (uint cull_index = local_index; cull_index < CSConst.uiLightCount; cull_index += NUM_THREADS) {
		const uint light_index = (cull_index == CSConst.uiReplacedLight) ? CSConst.uiReplacementSlot : cull_index;
		Light light = Lights[light_index];

		bool intersects_tile = false;

		if (light.uiLightType == FX_LIGHT_TYPE_DIRECTIONAL) {
			// Directional lights affect every tile
			intersects_tile = true;
		}
		else {
			// Point lights are bounded by their radius, spot lights by a sphere around their cone
			float3 bounds_center = light.vLightPosition;
			float bounds_radius = light.fLightRadius;

			if (light.uiLightType == FX_LIGHT_TYPE_SPOT) {
				const float4 spot_bounds = GetSpotBoundingSphere(light);

				bounds_center = spot_bounds.xyz;
				bounds_radius = spot_bounds.w;
			}

			float4 center_clip = mul(float4(bounds_center, 1.0), CSConst.mViewProjection);

			if (center_clip.w < -bounds_radius) {
				intersects_tile = false;
			}
			else if (center_clip.w <= bounds_radius) {
				intersects_tile = true;
			}
			else {
				float2 center_screen = ProjectToScreen(center_clip);

				// Approximate the screen space radius by projecting offset points
				float radius_x = length(ProjectToScreen(mul(float4(bounds_center + float3(bounds_radius, 0.0, 0.0), 1.0),
																CSConst.mViewProjection)) -
										center_screen);
				float radius_y = length(ProjectToScreen(mul(float4(bounds_center + float3(0.0, bounds_radius, 0.0), 1.0),
																CSConst.mViewProjection)) -
										center_screen);

				float radius = max(radius_x, radius_y);

				// Circle vs AABB intersection test
				float2 closest_point = clamp(center_screen, tile_min, tile_max);
				float2 distance_sq = center_screen - closest_point;

				intersects_tile = dot(distance_sq, distance_sq) <= (radius * radius);
			}
		}

		if (intersects_tile) {
			uint slot_index;
			InterlockedAdd(sTileLightCount, 1, slot_index);

			if (slot_index < MAX_LIGHTS_PER_TILE) {
				sTileLightIndices[slot_index] = light_index;
			}
		}
	}

	// Decals are stored as a bitmask rather than a list, so the forward pass walks them in the order they were
	// uploaded (oldest first) and newer decals always end up on top.
	const uint decal_count = min(CSConst.uiDecalCount, MAX_VISIBLE_DECALS);

	for (uint decal_index = local_index; decal_index < decal_count; decal_index += NUM_THREADS) {
		if (DecalIntersectsTile(bDecals[decal_index], float2(tile_min), float2(tile_max))) {
			InterlockedOr(sTileDecalMask[decal_index / 32], 1u << (decal_index % 32));
		}
	}

	GroupMemoryBarrierWithGroupSync();

	const uint tile_index = group_id.x + (group_id.y * CSConst.uiTileColumns);
	const uint start_index = tile_index * MAX_LIGHTS_PER_TILE;
	const uint light_count = min(sTileLightCount, MAX_LIGHTS_PER_TILE);

	if (local_index == 0) {
		bLightGrid[tile_index].Count = light_count;
		bLightGrid[tile_index].StartIndex = start_index;

		// The forward pass only walks the words that have decals in them
		uint word_start = DECAL_MASK_WORDS;
		uint word_end = 0;

		for (uint word = 0; word < DECAL_MASK_WORDS; word++) {
			if (sTileDecalMask[word] != 0) {
				word_start = min(word_start, word);
				word_end = word + 1;
			}
		}

		bLightGrid[tile_index].DecalWordStart = min(word_start, word_end);
		bLightGrid[tile_index].DecalWordEnd = word_end;
	}

	for (uint mask_word = local_index; mask_word < DECAL_MASK_WORDS; mask_word += NUM_THREADS) {
		bDecalMasks[(tile_index * DECAL_MASK_WORDS) + mask_word] = sTileDecalMask[mask_word];
	}

	// Each tile owns a fixed region of the global index list, copy the shared list into it
	for (uint copy_index = local_index; copy_index < light_count; copy_index += NUM_THREADS) {
		bLightIndexList[start_index + copy_index] = sTileLightIndices[copy_index];
	}
}
