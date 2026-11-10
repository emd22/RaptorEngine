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

F_RWStructBuffer(bDecalMasks, uint, 6, 0);

PERMIF(USE_DEPTH_BOUNDS);
F_Texture2D(tDepth, 2, 0);
PERMEND();

struct CSConsts
{
	float4x4 mViewProjection;
	float2 vScreenSize;
	uint uiLightCount;
	uint uiTileColumns;

	/// Light that is swapped out for `uiReplacementSlot`, 0xFFFFFFFF for no light
	uint uiReplacedLight;
	uint uiReplacementSlot;

	uint uiDecalCount;

	/// Turn stored depth back into view depth, see LinearViewDepth()
	float fDepthA;
	float fDepthB;
};

F_FrameData(CSConsts, CSConst, 1);

#define NUM_THREADS (LIGHT_TILE_SIZE * LIGHT_TILE_SIZE)
#define LIGHT_MASK_WORDS ((LIGHT_COUNT + 31) / 32)

// Same as the fog's FROXEL_NO_SURFACE, sky and viewmodel pixels leave the tile unbounded
#define TILE_NO_SURFACE 60000.0
#define VIEWMODEL_MIN_RAW_DEPTH 0.99

PERMIF(USE_DEPTH_BOUNDS);
#define TILE_PLANE_COUNT 6
PERMELSE();
#define TILE_PLANE_COUNT 5
PERMEND();

groupshared uint sDirectionalMask[LIGHT_MASK_WORDS];
groupshared uint sLocalMask[LIGHT_MASK_WORDS];
groupshared uint sTileDecalMask[DECAL_MASK_WORDS];

PERMIF(USE_DEPTH_BOUNDS);
/// Farthest view depth in the tile as float bits, which order the same as uints for positive floats
groupshared uint sTileFarDepth;

float SurfaceViewDepth(float raw_depth)
{
	const float view_depth = CSConst.fDepthB / max(raw_depth - CSConst.fDepthA, 1e-12);
	const bool has_surface = and(raw_depth > 0.0, raw_depth < VIEWMODEL_MIN_RAW_DEPTH);

	return select(has_surface, min(view_depth, TILE_NO_SURFACE), TILE_NO_SURFACE);
}
PERMEND();

// World space planes bounding the part of the view a tile covers, normalized and facing inwards (Gribb & Hartmann).
// `tile_min` and `tile_max` are in screen pixels. With depth bounds the tile also ends at `far_depth`, only a far plane
// since transparent surfaces aren't in the prepass.
void GetTilePlanes(float2 tile_min, float2 tile_max, float far_depth, out float4 planes[TILE_PLANE_COUNT])
{
	// clip = mul(float4(p, 1), VP), so each clip component is a dot product with one column of VP
	const float4x4 vp_columns = transpose(CSConst.mViewProjection);

	const float2 ndc_min = (tile_min / CSConst.vScreenSize) * 2.0 - 1.0;
	const float2 ndc_max = (tile_max / CSConst.vScreenSize) * 2.0 - 1.0;

	// Inside the tile is ndc_min * w <= xy <= ndc_max * w
	planes[0] = vp_columns[0] - (ndc_min.x * vp_columns[3]);
	planes[1] = (ndc_max.x * vp_columns[3]) - vp_columns[0];
	planes[2] = vp_columns[1] - (ndc_min.y * vp_columns[3]);
	planes[3] = (ndc_max.y * vp_columns[3]) - vp_columns[1];

	// w >= 0, in front of the eye
	planes[4] = vp_columns[3];

PERMIF(USE_DEPTH_BOUNDS);
	// w <= far_depth, w being the view depth
	planes[5] = float4(-vp_columns[3].xyz, far_depth - vp_columns[3].w);
PERMEND();

	[unroll]
	for (uint plane = 0; plane < TILE_PLANE_COUNT; plane++) {
		planes[plane] /= length(planes[plane].xyz);
	}
}

bool SphereIntersectsTile(float4 planes[TILE_PLANE_COUNT], float3 center, float radius)
{
	const float4 center_h = float4(center, 1.0);

	bool outside = false;

	[unroll]
	for (uint plane = 0; plane < TILE_PLANE_COUNT; plane++) {
		outside = or(outside, dot(center_h, planes[plane]) < -radius);
	}

	return !outside;
}

bool DecalIntersectsTile(float4 planes[TILE_PLANE_COUNT], Decal decal)
{
	const float4 center_h = float4(decal.vCenter, 1.0);

	// The axes are stored unit length, so they are scaled back to half extents here
	const float3 half_axis_x = decal.vAxisX * decal.vHalfExtents.x;
	const float3 half_axis_y = decal.vAxisY * decal.vHalfExtents.y;
	const float3 half_axis_z = decal.vAxisZ * decal.vHalfExtents.z;

	bool outside = false;

	[unroll]
	for (uint plane = 0; plane < TILE_PLANE_COUNT; plane++) {
		const float3 normal = planes[plane].xyz;

		// How far the box reaches from its center along the plane normal
		const float reach = abs(dot(normal, half_axis_x)) + abs(dot(normal, half_axis_y)) +
							abs(dot(normal, half_axis_z));

		outside = or(outside, dot(center_h, planes[plane]) < -reach);
	}

	return !outside;
}

[numthreads(LIGHT_TILE_SIZE, LIGHT_TILE_SIZE, 1)]
void main(uint3 group_id : SV_GroupID, uint3 thread_id : SV_GroupThreadID)
{
	const uint local_index = thread_id.y * LIGHT_TILE_SIZE + thread_id.x;

	for (uint clear_light = local_index; clear_light < LIGHT_MASK_WORDS; clear_light += NUM_THREADS) {
		sDirectionalMask[clear_light] = 0;
		sLocalMask[clear_light] = 0;
	}

	for (uint clear_word = local_index; clear_word < DECAL_MASK_WORDS; clear_word += NUM_THREADS) {
		sTileDecalMask[clear_word] = 0;
	}

PERMIF(USE_DEPTH_BOUNDS);
	if (local_index == 0) {
		sTileFarDepth = 0;
	}
PERMEND();

	GroupMemoryBarrierWithGroupSync();

	// Tile bounds in screen pixels
	const uint2 tile_min = group_id.xy * LIGHT_TILE_SIZE;
	const uint2 tile_max = min(tile_min + LIGHT_TILE_SIZE, uint2(CSConst.vScreenSize));

PERMIF(USE_DEPTH_BOUNDS);
	// One pixel per thread, threads past the edge of the screen repeat the last one
	const uint2 depth_pixel = min(tile_min + thread_id.xy, tile_max - 1);
	InterlockedMax(sTileFarDepth, asuint(SurfaceViewDepth(F_SampleLoad(tDepth, int3(depth_pixel, 0)).r)));

	GroupMemoryBarrierWithGroupSync();

	const float far_depth = asfloat(sTileFarDepth);
PERMELSE();
	const float far_depth = TILE_NO_SURFACE;
PERMEND();

	float4 tile_planes[TILE_PLANE_COUNT];
	GetTilePlanes(float2(tile_min), float2(tile_max), far_depth, tile_planes);

	for (uint cull_index = local_index; cull_index < CSConst.uiLightCount; cull_index += NUM_THREADS) {
		const uint light_index = select(cull_index == CSConst.uiReplacedLight, CSConst.uiReplacementSlot, cull_index);
		const uint light_bit = 1u << (light_index % 32);

		Light light = Lights[light_index];

		if (light.uiLightType == FX_LIGHT_TYPE_DIRECTIONAL) {
			// Directional lights affect every tile
			InterlockedOr(sDirectionalMask[light_index / 32], light_bit);
			continue;
		}

		// Point lights are bounded by their radius, spot lights by a sphere around their cone
		// Should probably implement cone tests here in the future to reduce the amount of tiles a spotlight shows up in
		const bool is_spot = light.uiLightType == FX_LIGHT_TYPE_SPOT;
		const float4 bounds = select(is_spot, GetSpotBoundingSphere(light),
									 float4(light.vLightPosition, light.fLightRadius));

		if (SphereIntersectsTile(tile_planes, bounds.xyz, bounds.w)) {
			InterlockedOr(sLocalMask[light_index / 32], light_bit);
		}
	}

	// Decals are stored as a bitmask rather than a list, so the forward pass walks them in the order they were
	// uploaded (oldest first) and newer decals always end up on top.
	const uint decal_count = min(CSConst.uiDecalCount, MAX_VISIBLE_DECALS);

	for (uint decal_index = local_index; decal_index < decal_count; decal_index += NUM_THREADS) {
		if (DecalIntersectsTile(tile_planes, bDecals[decal_index])) {
			InterlockedOr(sTileDecalMask[decal_index / 32], 1u << (decal_index % 32));
		}
	}

	GroupMemoryBarrierWithGroupSync();

	uint directional_count = 0;
	uint local_count = 0;

	for (uint count_word = 0; count_word < LIGHT_MASK_WORDS; count_word++) {
		directional_count += countbits(sDirectionalMask[count_word]);
		local_count += countbits(sLocalMask[count_word]);
	}

	const uint tile_index = group_id.x + (group_id.y * CSConst.uiTileColumns);
	const uint start_index = tile_index * MAX_LIGHTS_PER_TILE;
	const uint light_count = min(directional_count + local_count, MAX_LIGHTS_PER_TILE);

	if (local_index == 0) {
		bLightGrid[tile_index].Count = light_count;
		bLightGrid[tile_index].StartIndex = start_index;

		// The forward pass only walks the words that have decals in them
		uint word_start = DECAL_MASK_WORDS;
		uint word_end = 0;

		for (uint word = 0; word < DECAL_MASK_WORDS; word++) {
			const bool has_decals = sTileDecalMask[word] != 0;

			word_start = select(has_decals, min(word_start, word), word_start);
			word_end = select(has_decals, word + 1, word_end);
		}

		word_start = min(word_start, word_end);

		bLightGrid[tile_index].DecalWordStart = word_start;
		bLightGrid[tile_index].DecalWordEnd = word_end;

		for (uint mask_word = word_start; mask_word < word_end; mask_word++) {
			bDecalMasks[(tile_index * DECAL_MASK_WORDS) + mask_word] = sTileDecalMask[mask_word];
		}
	}

	for (uint slot = local_index; slot < LIGHT_COUNT; slot += NUM_THREADS) {
		const uint word = slot / 32;
		const uint bit = 1u << (slot % 32);
		const uint bits_below = bit - 1;

		const bool is_directional = (sDirectionalMask[word] & bit) != 0;
		const bool is_local = (sLocalMask[word] & bit) != 0;

		if (!or(is_directional, is_local)) {
			continue;
		}

		uint rank = countbits(select(is_directional, sDirectionalMask[word], sLocalMask[word]) & bits_below);

		for (uint prior_word = 0; prior_word < word; prior_word++) {
			rank += countbits(select(is_directional, sDirectionalMask[prior_word], sLocalMask[prior_word]));
		}

		rank += select(is_directional, 0u, directional_count);

		if (rank < MAX_LIGHTS_PER_TILE) {
			bLightIndexList[start_index + rank] = slot;
		}
	}
}
