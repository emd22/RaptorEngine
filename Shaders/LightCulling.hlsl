F_PROGRAM(FPT_COMPUTE)

#include "./Helper.hlsl"
#include "LightingCommon.hlsli"

F_CBuffer(FSLightBuffer, 4, 0)
{
	Light Lights[LIGHT_COUNT];
};

F_RWStructBuffer(bLightGrid, TileLightData, 0, 0);
F_RWStructBuffer(bLightIndexList, uint, 1, 0);

struct CSPushConsts
{
	float4x4 mViewProjection;
	float2 vScreenSize;
	uint uiLightCount;
	uint uiTileColumns;
};

[[vk::push_constant]] CSPushConsts CSConst;

#define NUM_THREADS (LIGHT_TILE_SIZE * LIGHT_TILE_SIZE)

groupshared uint sTileLightCount;
groupshared uint sTileLightIndices[MAX_LIGHTS_PER_TILE];

float2 ProjectToScreen(float4 clip_space)
{
	float2 ndc = clip_space.xy / clip_space.w;

	return (ndc * 0.5 + 0.5) * CSConst.vScreenSize;
}

[numthreads(LIGHT_TILE_SIZE, LIGHT_TILE_SIZE, 1)]
void main(uint3 group_id : SV_GroupID, uint3 thread_id : SV_GroupThreadID)
{
	const uint local_index = thread_id.y * LIGHT_TILE_SIZE + thread_id.x;

	if (local_index == 0) {
		sTileLightCount = 0;
	}

	GroupMemoryBarrierWithGroupSync();

	// Tile bounds in screen pixels
	const uint2 tile_min = group_id.xy * LIGHT_TILE_SIZE;
	const uint2 tile_max = min(tile_min + LIGHT_TILE_SIZE, uint2(CSConst.vScreenSize));

	for (uint light_index = local_index; light_index < CSConst.uiLightCount; light_index += NUM_THREADS) {
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

	GroupMemoryBarrierWithGroupSync();

	const uint tile_index = group_id.x + (group_id.y * CSConst.uiTileColumns);
	const uint start_index = tile_index * MAX_LIGHTS_PER_TILE;
	const uint light_count = min(sTileLightCount, MAX_LIGHTS_PER_TILE);

	if (local_index == 0) {
		bLightGrid[tile_index].Count = light_count;
		bLightGrid[tile_index].StartIndex = start_index;
	}

	// Each tile owns a fixed region of the global index list, copy the shared list into it
	for (uint copy_index = local_index; copy_index < light_count; copy_index += NUM_THREADS) {
		bLightIndexList[start_index + copy_index] = sTileLightIndices[copy_index];
	}
}
