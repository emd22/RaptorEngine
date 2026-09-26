#pragma once

#include <Math/Vec2.hpp>
#include <Renderer/PipelineNames.hpp>
#include <Renderer/RenderStage.hpp>

namespace fx::renderer {

using ShadowTileIndex = uint32;

static constexpr ShadowTileIndex ShadowTileIndexNull = UINT32_MAX;

struct alignas(16) ShadowPushConstants
{
	float32 CameraMatrix[16];
	uint32 ObjectIndex = 0;
};

/// A rectangle of the shadow atlas, in texels
struct ShadowAtlasRegion
{
	Vec2u Offset = Vec2u::sZero;
	Vec2u Size = Vec2u::sZero;
};

class ShadowAtlas
{
public:
	static constexpr uint32 scWidth = 4096;
	static constexpr uint32 scHeight = 2048;

	static constexpr uint32 scDirectionalSize = 2048;
	static constexpr uint32 scSpotTileSize = 512;

	static constexpr uint32 scSpotTilesPerRow = (scWidth - scDirectionalSize) / scSpotTileSize;
	static constexpr uint32 scMaxSpotTiles = scSpotTilesPerRow * (scHeight / scSpotTileSize);

	static_assert(scDirectionalSize <= scHeight, "The directional region must fit in the atlas");

	static_assert(scWidth == 4096 && scHeight == 2048, "Update SHADOW_ATLAS_WIDTH/HEIGHT in Shaders/Helper.hlsl");
	static_assert(scMaxSpotTiles <= 32, "Spot tiles are tracked with a 32 bit mask");

public:
	ShadowAtlas();

	/**
	 * @brief Starts a shadow pass that clears `region`, binds the shadow pipeline and points the viewport and scissor
	 * at the region. The first pass after the atlas was invalidated clears the whole atlas instead.
	 */
	void BeginRegion(const ShadowAtlasRegion& region);
	void EndRegion();

	/**
	 * @brief Binds a shadow pipeline in the middle of a region, keeping the viewport and scissor on the region.
	 */
	void BindPipeline(ePipelineName name);

	ShadowAtlasRegion GetDirectionalRegion() const;
	ShadowAtlasRegion GetSpotTileRegion(ShadowTileIndex tile) const;

	/**
	 * @brief Writes the scale (xy) and offset (zw) that take a UV inside of `region` to a UV in the whole atlas.
	 */
	void GetRegionUVTransform(const ShadowAtlasRegion& region, float32 out_transform[4]) const;

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
