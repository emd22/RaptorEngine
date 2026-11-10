#ifndef SDF_PAGE_COMMON_HLSLI
#define SDF_PAGE_COMMON_HLSLI

/// Mirrors SDFPageScene in Src/Renderer/SDFPageScene.hpp
#define SDF_PAGE_MAX_LEVELS 5
#define SDF_PAGE_CLIP 64
#define SDF_PAGE_SLOTS_PER_LEVEL (SDF_PAGE_CLIP * SDF_PAGE_CLIP * SDF_PAGE_CLIP)
#define SDF_PAGE_TEXELS 8
#define SDF_PAGE_EMPTY 0xFFFFFFFFu
#define SDF_PAGE_PENDING 0xFFFFFFFEu

/// Atlas size in pages, 64 x 32 x 32
#define SDF_PAGE_ATLAS_SHIFT_X 6
#define SDF_PAGE_ATLAS_SHIFT_Y 5

/// Mirrors CompiledMapSDFPageTriangle in Src/Map/CompiledMap.hpp
struct SDFPageTriangle
{
	float4 vA;
	float4 vB;
	float4 vC;
};

/// Mirrors SDFPageLevelGpu
struct SDFPageLevel
{
	int4 vOrigin;
	/// Page size, its reciprocal, the margin distances are kept up to, and the texel size
	float4 vParams;
};

/// Mirrors SDFPageSceneGpu
struct SDFPageScene
{
	SDFPageLevel Levels[SDF_PAGE_MAX_LEVELS];
	/// Level count, then the atlas size in pages
	uint4 vInfo;
	float4 vInvAtlasSize;
};

float3 SDFPageAtlasOrigin(uint atlas)
{
	const uint x = atlas & ((1u << SDF_PAGE_ATLAS_SHIFT_X) - 1u);
	const uint y = (atlas >> SDF_PAGE_ATLAS_SHIFT_X) & ((1u << SDF_PAGE_ATLAS_SHIFT_Y) - 1u);
	const uint z = atlas >> (SDF_PAGE_ATLAS_SHIFT_X + SDF_PAGE_ATLAS_SHIFT_Y);

	return float3(uint3(x, y, z) * SDF_PAGE_TEXELS);
}

uint SDFPageSlotIndex(uint level, int3 coord)
{
	const uint3 slot = uint3(coord & (SDF_PAGE_CLIP - 1));

	return level * SDF_PAGE_SLOTS_PER_LEVEL + slot.x + slot.y * SDF_PAGE_CLIP + slot.z * SDF_PAGE_CLIP * SDF_PAGE_CLIP;
}

bool SDFPageInWindow(SDFPageLevel level, int3 coord)
{
	return and(all(coord >= level.vOrigin.xyz), all(coord < level.vOrigin.xyz + SDF_PAGE_CLIP));
}

#endif
