#pragma once

#include <Math/Rect.hpp>
#include <Math/Vec2.hpp>
#include <Renderer/PipelineDesc.hpp>
#include <Renderer/PipelineKey.hpp>
#include <Renderer/PipelineNames.hpp>
#include <Renderer/RenderStage.hpp>
#include <bit>

namespace fx::renderer {

using ShadowTileIndex = uint32;

static constexpr ShadowTileIndex ShadowTileIndexNull = UINT32_MAX;

struct alignas(16) ShadowViewConstants
{
	float32 ViewProjection[16];
};

struct ShadowPushConstants
{
	uint32 ObjectIndex = 0;
	uint32 BoneBase = 0;
	uint32 MaterialIndex = 0;
};

static constexpr uint32 TileBit(uint32 tile_index) { return (1U << tile_index); }


enum class eShadowAtlasMask : uint32
{
	None = 0x00000000,

	// 12 tiles
	StaticSpot = 0x0000CCFF,

	FarCascade = TileBit(8) | TileBit(9) | TileBit(12) | TileBit(13),
};


class ShadowAtlas
{
public:
	/// The total size of the shadow atlas texture
	static constexpr Vec2u scAtlasSize = Vec2u(4096, 2048);

	/// The size of the main sun's directional shadow map
	static constexpr uint32 scDirectionalSize = scAtlasSize.X / 2;

	/// The size of each spotlight shadow map
	static constexpr uint32 scSpotTileSize = 512;

	/// The size of the far cascade of the directional shadow map.
	static constexpr uint32 scFarDirectionalSize = 1024;


	/// The entire section of the atlas that is available to spotlights. Note that this is inclusive of the far cascade
	/// (not spotlights) and the dynamic spotlight tiles. This is the area of the atlas in pixels.
	static constexpr Vec2u scTotalSpotTilesArea = Vec2u(scAtlasSize.X - scDirectionalSize, scAtlasSize.Y);


	/// The number of static spotlight tiles that are available in both X and Y directions.
	/// Note that this is the number of tiles, not the pixel dimensions. In general, this is (4, 2).
	static constexpr Vec2u scStaticSpotTiles = Vec2u(scTotalSpotTilesArea.X, scTotalSpotTilesArea.Y / 2U) /
											   Vec2u(scSpotTileSize);

	/// The number of dynamic spotlight tiles that are available in both X and Y directions.
	/// Note that this is the number of tiles, not the pixel dimensions. In general, this is (2, 2).
	static constexpr Vec2u scDynamicSpotTiles = Vec2u(scTotalSpotTilesArea.X / 2U, scTotalSpotTilesArea.Y / 2U) /
												Vec2u(scSpotTileSize);

	/// The number of spot tiles across one row of the grid to the right of the sun.
	static constexpr uint32 scSpotGridColumns = scTotalSpotTilesArea.X / scSpotTileSize;

	static constexpr uint32 scFarTilesPerSide = scFarDirectionalSize / scSpotTileSize;

	static constexpr uint32 scTotalStaticSpotTiles = (scStaticSpotTiles.X * scStaticSpotTiles.Y) +
													 (scDynamicSpotTiles.X * scDynamicSpotTiles.Y);
	// static constexpr uint32 scTotalDynamicSpotTiles = (scDynamicSpotTiles.X * scDynamicSpotTiles.Y);

	static_assert(scDirectionalSize <= scAtlasSize.Y, "The directional region must fit into the atlas");

	static_assert(scAtlasSize.X == 4096 && scAtlasSize.Y == 2048,
				  "You changed the shadow atlas dimensions, update SHADOW_ATLAS_WIDTH/HEIGHT in Shaders/Helper.hlsl!");


	static_assert((static_cast<uint32>(eShadowAtlasMask::StaticSpot) &
				   static_cast<uint32>(eShadowAtlasMask::FarCascade)) == 0U,
				  "Static spot tiles and the far cascade overlap");

	static_assert(std::popcount(static_cast<uint32>(eShadowAtlasMask::StaticSpot)) == scTotalStaticSpotTiles,
				  "The static spot mask does not match the static spot tile region");
	// static_assert(std::popcount(static_cast<uint32>(eShadowAtlasMask::DynamicSpot)) == scTotalDynamicSpotTiles,
	// 			  "The dynamic spot mask does not match the dynamic spot tile region");
	static_assert(std::popcount(static_cast<uint32>(eShadowAtlasMask::FarCascade)) ==
					  scFarTilesPerSide * scFarTilesPerSide,
				  "The far cascade mask does not match the far cascade region");

	static_assert(scTotalStaticSpotTiles <= 32,
				  "Not enough bits for spot tiles. There shouldn't be 32 shadow casting spotlights on screen at once!");

public:
	ShadowAtlas();

	/**
	 * @brief Starts a shadow pass that clears `region`, binds the shadow pipeline and points the viewport and scissor
	 * at the region. The first pass after the atlas was invalidated clears the whole atlas instead.
	 */
	void BeginRegion(const URect& region);
	void EndRegion();

	/**
	 * @brief Binds a shadow pipeline in the middle of a region, keeping the viewport and scissor on the region.
	 */
	void BindPipeline(PipelineHandle pipeline);

	bool MakeShadowDesc(ePipelineFeatures features, PipelineDesc& out_desc);

	URect GetDirectionalRegion() const;
	URect GetFarDirectionalRegion() const;
	URect GetDynamicSpotTileRegion() const;
	URect GetStaticSpotTileRegion() const;

	URect GetSpotTileRegion(ShadowTileIndex tile) const;

	/**
	 * @brief Writes the scale (xy) and offset (zw) that take a UV inside of `region` to a UV in the whole atlas.
	 */
	void GetRegionUVTransform(const URect& region, float32 out_transform[4]) const;

	/**
	 * @brief Finds and allocates a new static spotlight tile. Returns ShadowTileIndexNull if none are found.
	 */
	ShadowTileIndex AllocateStaticSpotTile();

	/**
	 * @brief Finds and allocates a new dynamic spotlight tile. Returns ShadowTileIndexNull if none are found.
	 */
	// ShadowTileIndex AllocateDynamicSpotTile();

	/**
	 * @brief Returns the lowest free tile inside `mask` without allocating it, or ShadowTileIndexNull if there is none.
	 */
	ShadowTileIndex FindFreeTile(const eShadowAtlasMask mask) const;

	/**
	 * @brief Frees a spot tile from the shadow atlas. Tiles outside the static and dynamic spot masks are ignored.
	 */
	void FreeSpotTile(ShadowTileIndex tile);

	/**
	 * @brief Invalidates everything baked into the atlas, with it being cleared at the next BeginRegion() making every
	 * spot light rebake
	 */
	void Invalidate();

	/// Changes whenever the atlas contents are lost. Bakes from an older generation are no longer in the atlas.
	FX_FORCE_INLINE uint32 GetGeneration() const { return mGeneration; }

	/// Returns true once the shadow atlas has been rendered into and can be sampled.
	FX_FORCE_INLINE bool IsInitialized() const { return mbInitialized; }

	Target* GetTarget();

	~ShadowAtlas() = default;

public:
	RenderStage RenderStage;

private:
	// Mini bitmap for the spotlights
	uint32 mSpotTilesInUse = 0;

	uint32 mGeneration = 0;

	bool mbNeedsClear = true;
	bool mbInitialized = false;
};

} // namespace fx::renderer
