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


class ShadowAtlas
{
public:
	static constexpr uint32 scWidth = 4096;
	static constexpr uint32 scHeight = 2048;

	static constexpr uint32 scDirectionalSize = 2048;
	static constexpr uint32 scSpotTileSize = 512;

	static constexpr uint32 scSpotTilesPerRow = (scWidth - scDirectionalSize) / scSpotTileSize;
	static constexpr uint32 scSpotTileRows = scHeight / scSpotTileSize;
	static constexpr uint32 scMaxSpotTiles = scSpotTilesPerRow * scSpotTileRows;

	static constexpr uint32 scFarDirectionalSize = 1024;
	static constexpr uint32 scFarTilesPerSide = scFarDirectionalSize / scSpotTileSize;

	static constexpr uint32 scFarTileMask = []
	{
		uint32 mask = 0;

		for (uint32 row = scSpotTileRows - scFarTilesPerSide; row < scSpotTileRows; row++) {
			for (uint32 column = scSpotTilesPerRow - scFarTilesPerSide; column < scSpotTilesPerRow; column++) {
				mask |= (1U << ((row * scSpotTilesPerRow) + column));
			}
		}

		return mask;
	}();
	static constexpr uint32 scUsableSpotTiles = scMaxSpotTiles - std::popcount(scFarTileMask);

	static_assert(scDirectionalSize <= scHeight, "The directional region must fit in the atlas");

	static_assert(scWidth == 4096 && scHeight == 2048, "Update SHADOW_ATLAS_WIDTH/HEIGHT in Shaders/Helper.hlsl");
	static_assert(scMaxSpotTiles <= 32,
				  "Not enough bits for spot tiles. There shouldn't be 32 shadow casting spotlights on screen at once!");
	static_assert(scFarDirectionalSize % scSpotTileSize == 0, "The far region must cover whole spot tiles");
	static_assert(scFarTilesPerSide <= scSpotTilesPerRow && scFarTilesPerSide <= scSpotTileRows,
				  "The far region must fit in the spot tile area");

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
	URect GetSpotTileRegion(ShadowTileIndex tile) const;

	/**
	 * @brief Writes the scale (xy) and offset (zw) that take a UV inside of `region` to a UV in the whole atlas.
	 */
	void GetRegionUVTransform(const URect& region, float32 out_transform[4]) const;

	/// Returns ShadowTileIndexNull if every tile is in use.
	ShadowTileIndex AllocateSpotTile();
	void FreeSpotTile(ShadowTileIndex tile);

	/**
	 * @brief Invalidates everything baked into the atlas, with it being cleared at the next BeginRegion() making every
	 * spot light rebake
	 */
	void Invalidate();

	/// Changes whenever the atlas contents are lost. Bakes from an older generation are no longer in the atlas.
	FX_FORCE_INLINE uint32 GetGeneration() const { return mGeneration; }

	/// Returns true if the shadow atlas is ready to be sampled
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
