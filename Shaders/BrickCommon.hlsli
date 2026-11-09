#ifndef BRICK_COMMON_HLSLI
#define BRICK_COMMON_HLSLI

/// Mirrors BrickScene in Src/Renderer/BrickScene.hpp
#define BRICK_MAX_LEVELS 5
#define BRICK_CLIP 64
#define BRICK_SLOTS_PER_LEVEL (BRICK_CLIP * BRICK_CLIP * BRICK_CLIP)
#define BRICK_TEXELS 8
#define BRICK_EMPTY 0xFFFFFFFFu
#define BRICK_PENDING 0xFFFFFFFEu

/// Atlas size in bricks, 64 x 32 x 32
#define BRICK_ATLAS_SHIFT_X 6
#define BRICK_ATLAS_SHIFT_Y 5

/// Mirrors CompiledMapBrickTriangle in Src/Map/CompiledMap.hpp
struct BrickTriangle
{
	float4 vA;
	float4 vB;
	float4 vC;
};

/// Mirrors BrickLevelGpu
struct BrickLevel
{
	int4 vOrigin;
	/// Brick size, its reciprocal, the margin distances are kept up to, and the texel size
	float4 vParams;
};

/// Mirrors BrickSceneGpu
struct BrickScene
{
	BrickLevel Levels[BRICK_MAX_LEVELS];
	/// Level count, then the atlas size in bricks
	uint4 vInfo;
	float4 vInvAtlasSize;
};

float3 BrickAtlasOrigin(uint atlas)
{
	const uint x = atlas & ((1u << BRICK_ATLAS_SHIFT_X) - 1u);
	const uint y = (atlas >> BRICK_ATLAS_SHIFT_X) & ((1u << BRICK_ATLAS_SHIFT_Y) - 1u);
	const uint z = atlas >> (BRICK_ATLAS_SHIFT_X + BRICK_ATLAS_SHIFT_Y);

	return float3(uint3(x, y, z) * BRICK_TEXELS);
}

uint BrickSlotIndex(uint level, int3 coord)
{
	const uint3 slot = uint3(coord & (BRICK_CLIP - 1));

	return level * BRICK_SLOTS_PER_LEVEL + slot.x + slot.y * BRICK_CLIP + slot.z * BRICK_CLIP * BRICK_CLIP;
}

bool BrickInWindow(BrickLevel level, int3 coord)
{
	return and(all(coord >= level.vOrigin.xyz), all(coord < level.vOrigin.xyz + BRICK_CLIP));
}

#endif
