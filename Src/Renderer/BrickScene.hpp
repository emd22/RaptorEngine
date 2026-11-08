/*
 * File:        BrickScene.hpp
 * Author:      emd22
 * Created:     08/11/2026
 * Description: The sparse distance field that the GI traces against. Every clip level is a 64^3 window of bricks
 *              around the camera; the bricks that mapc found geometry in are given 8^3 texels in a shared atlas and
 *              their distances are computed on the GPU from the triangles that mapc listed for them.
 */

#pragma once

#include "Backend/GpuBuffer.hpp"

#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Map/CompiledMap.hpp>
#include <Math/Vec3.hpp>

namespace fx {
class Image;
class BaseCamera;
} // namespace fx

namespace fx::renderer {

class CommandBuffer;
struct Target;

/// Laid out to match BrickLevel in Shaders/BrickCommon.hlsli
struct BrickLevelGpu
{
	int32 Origin[4] = {};
	/// Brick size, its reciprocal, the margin distances are kept up to, and the texel size
	float32 Params[4] = {};
};

/// Laid out to match BrickScene in Shaders/BrickCommon.hlsli
struct BrickSceneGpu
{
	static constexpr uint32 scMaxLevels = 5;

	BrickLevelGpu Levels[scMaxLevels];
	/// Level count, then the atlas size in bricks
	uint32 Info[4] = {};
	float32 InvAtlasSize[4] = {};
};

struct BrickCheckStats
{
	uint32 Match = 0;
	uint32 Early = 0;
	uint32 Late = 0;
	uint32 Sky = 0;
	uint32 Skipped = 0;
};

class BrickScene
{
public:
	static constexpr uint32 scClipBricks = 64;
	static constexpr uint32 scSlotsPerLevel = scClipBricks * scClipBricks * scClipBricks;
	static constexpr uint32 scMaxLevels = BrickSceneGpu::scMaxLevels;
	static constexpr uint32 scTexelsPerBrick = 8;

	static constexpr uint32 scAtlasBricksX = 64;
	static constexpr uint32 scAtlasBricksY = 32;
	static constexpr uint32 scAtlasBricksZ = 32;
	static constexpr uint32 scAtlasBrickCount = scAtlasBricksX * scAtlasBricksY * scAtlasBricksZ;
	// BRICK_ATLAS_SHIFT_X/Y in Shaders/BrickCommon.hlsli
	static_assert(scAtlasBricksX == 64 && scAtlasBricksY == 32);

	static constexpr uint32 scMaxJobsPerFrame = 65536;

	/// Indirection values that are not an atlas brick
	static constexpr uint32 scEmpty = 0xFFFFFFFF;
	static constexpr uint32 scPending = 0xFFFFFFFE;

	enum class eDebugView : uint32
	{
		None,
		Normals,
		Steps,
		GiRays,
		DepthCheck,
		Motion,
		Roughness,
		Levels,
		GiSteps,
		ProbeRays,
		Count,
	};

public:
	/// Makes the fixed size buffers and the atlas, and placeholders for the per map ones
	void Create();
	void CreatePipelines(Target* depth, Target* normals, Target* motion, Target* lit);
	void Destroy();

	/// Takes the compiled map's bricks. Waits for the GPU to be idle.
	bool Upload(const CompiledMapBricks& bricks);
	void Clear();

	/**
	 * @brief Moves the clip windows to `center` and records the pass that fills the bricks that came into view
	 * @param eval_budget The most bricks to compute distances for this frame
	 */
	void Update(CommandBuffer& cmd, const Vec3f& center, uint32 eval_budget);

	/// Traces from the camera (or from the prepass surfaces) and overwrites the lit target with the result
	void RenderDebug(CommandBuffer& cmd, const BaseCamera& camera, eDebugView view, float32 cone_slope, float32 ray_length);

	bool IsActive() const { return mbActive; }

	void GetSceneConsts(BrickSceneGpu& out_consts) const;

	uint32 GetResidentCount() const { return scAtlasBrickCount - static_cast<uint32>(mFreeAtlas.Size); }
	uint32 GetPendingCount() const;
	const BrickCheckStats& GetCheckStats() const { return mCheckStats; }

private:
	struct Level
	{
		float32 BrickSize = 0.0f;
		uint32 FirstBrick = 0;
		uint32 BrickCount = 0;

		int32 Origin[3] = {};
		bool bPlaced = false;

		/// Atlas brick of every slot, or scEmpty
		SizedArray<uint32> SlotAtlas;
		/// The compiled brick the slot should hold, or scEmpty
		SizedArray<uint32> SlotBrick;
		SizedArray<uint8> SlotDirty;

		/// Slots waiting for an atlas brick, read from `PendingRead` on
		DynArray<uint32, GrowthFunctions::Double> Pending;
		uint32 PendingRead = 0;
	};

	struct Job
	{
		uint32 Slot = 0;
		uint32 Atlas = scEmpty;
		uint32 FirstRef = 0;
		uint32 RefCount = 0;
		float32 OriginAndTexel[4] = {};
	};

	void CreateBuffer(RawGpuBuffer& buffer, uint64 size, const void* data);

	/// Moves a level's window, returning true if it moved too far to keep any of its bricks
	bool PlaceLevel(uint32 level_index, const Vec3f& center);
	void ResetLevel(uint32 level_index);
	void MarkDirty(uint32 level_index, uint32 slot, uint8 value);
	void FreeSlot(Level& level, uint32 slot);

	/// The range of a level's bricks whose X coordinate is in [min_x, max_x)
	void FindBrickRange(const Level& level, int32 min_x, int32 max_x, uint32& out_begin, uint32& out_end) const;

	uint32 GetSlot(const int32 coord[3]) const;

private:
	RawGpuBuffer mRefs;
	RawGpuBuffer mTriangles;
	RawGpuBuffer mIndirection;
	RawGpuBuffer mJobs;
	RawGpuBuffer mStats;

	Image* mpAtlas = nullptr;
	bool mbAtlasInitialized = false;

	SizedArray<uint32> mFreeAtlas;

	/// CPU copy of the compiled bricks, to find the ones that come into view
	SizedArray<CompiledMapBrick> mBricks;

	Level mLevels[scMaxLevels];
	uint32 mLevelCount = 0;
	float32 mMarginScale = 0.5f;

	DynArray<uint32, GrowthFunctions::Double> mDirty;

	BrickCheckStats mCheckStats;
	uint32 mDebugFrame = 0;

	bool mbActive = false;
};

} // namespace fx::renderer
