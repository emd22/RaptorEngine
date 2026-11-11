/*
 * File:        ProbeClipmap.hpp
 * Author:      emd22
 * Created:     10/11/2026
 * Description: Runtime GI probes in camera centred toroidal levels, traced against the SDF pages. A fixed budget of
 *              probes is traced each frame and blended into octahedral irradiance and depth moment tiles.
 */

#pragma once

#include "Backend/GpuBuffer.hpp"

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Map/CompiledMap.hpp>
#include <Math/FVec3.hpp>

namespace fx {
class Image;
class BaseCamera;
} // namespace fx

namespace fx::renderer {

class CommandBuffer;
struct Target;
struct ShadeLightCandidate;

/// Laid out to match ClipmapLevel in Shaders/ProbeClipmapCommon.hlsli
struct ProbeClipmapLevelGpu
{
	int32 Origin[4] = {};
	/// Probe spacing, its reciprocal, ray length, cone slope
	float32 Params[4] = {};
};

class ProbeClipmap
{
public:
	/// Mirrors Shaders/ProbeClipmapCommon.hlsli
	static constexpr uint32 scLevelCount = 3;
	static constexpr int32 scGridX = 32;
	static constexpr int32 scGridY = 16;
	static constexpr int32 scGridZ = 32;
	static constexpr uint32 scProbesPerLevel = scGridX * scGridY * scGridZ;
	static constexpr uint32 scRayCount = 64;
	static constexpr uint32 scIrradianceSize = 8;
	static constexpr uint32 scMomentsSize0 = 16;
	static constexpr uint32 scMomentsSize = 16;
	static constexpr uint32 scTileColumns = scGridX * scGridY;

	/// How many of its next updates a fully dirty probe is traced first and blended faster for. Mirrors
	/// CLIPMAP_DIRTY_UPDATES in Shaders/ProbeClipmapConsts.hlsli, and has to fit the job's four dirty bits.
	static constexpr uint32 scDirtyUpdates = 8;

	static constexpr float32 scLevel0Spacing = 1.0f;
	static constexpr uint32 scMaxJobsPerFrame = 8192;

	/// Sizes of one frame's part of the paged buffers, and of the probe data
	static constexpr uint64 scStateFrameSize = static_cast<uint64>(scLevelCount) * scProbesPerLevel * sizeof(uint32);
	/// The probe data is per grid point, wrapping twice as wide as the window in X and Z. Mirrors
	/// CLIPMAP_PROBE_DATA_* in Shaders/ProbeClipmapCommon.hlsli
	static constexpr uint32 scProbeDataX = scGridX * 2;
	static constexpr uint32 scProbeDataY = scGridY;
	static constexpr uint32 scProbeDataZ = scGridZ * 2;
	static constexpr uint64 scProbeDataSize = static_cast<uint64>(scLevelCount) * scProbeDataX * scProbeDataY *
											  scProbeDataZ * sizeof(float32) * 4;
	static constexpr uint64 scForwardDataFrameSize = 256;

	enum class eDebugView : uint32
	{
		None,
		Spheres0,
		Surface,
		Levels,
		Spheres1,
		Spheres2,
		Visibility,
		Count,
	};

	struct Settings
	{
		/// Probes traced per frame
		uint32 Budget = 3072;
		/// The same on every level: a ray that runs out before it hits anything is lit by the sky, so levels with
		/// different lengths disagree about rooms longer than the shortest
		float32 RayLengths[scLevelCount] = { 30.0f, 30.0f, 30.0f };
		float32 ConeSlope = 0.1f;
		float32 Hysteresis = 0.97f;
		/// Hysteresis of the probes whose short term light is ChangeThreshold or more away from their stored light,
		/// relative to the larger of the two, fully reached at twice that
		float32 FastHysteresis = 0.8f;
		float32 ChangeThreshold = 0.3f;
		/// Hysteresis of a fully dirty probe. It eases back to the usual one over the probe's dirty updates.
		float32 DirtyHysteresis = 0.7f;
		/// Exponent of the cosine lobe each depth moment texel gathers over
		float32 MomentSharpness = 50.0f;
		/// Hits take the light that reached them from last frame's probes
		bool bBounce = true;
		/// Probes are moved out of walls and away from surfaces, and switched off where that fails
		bool bRelocate = true;
		/// Probes at grid points the compiled map marks as inside brushes or outside the map are never traced
		bool bUseMask = true;
		/// Radiance of the rays that miss, the same constant sky the mapc bake uses
		float32 SkyRadiance = 1500.0f / 3.14159265f;
	};

public:
	void Create();
	void Destroy();
	void CreatePipelines(Target* depth, Target* normals, Target* lit);

	/// Forgets every probe, for a new map
	void Reset();

	/// Takes a copy of the map's probe mask, or drops the one it has if `mask` is empty or built for other spacings.
	/// Reset() has to follow.
	void SetProbeMask(const CompiledMapProbeMask& mask);

	/**
	 * @brief Marks the probes within `radius` of `center` as lit differently than when they were traced, so they are
	 * traced first and blended faster. `strength` in (0, 1] is how much changed, 1 for a light switched on or off.
	 */
	void MarkDirty(const FVec3& center, float32 radius, float32 strength);
	void MarkAllDirty(float32 strength);

	/// Compares this frame's lights with the last ones it was given and marks the probes around those that changed
	void TrackLights(const SizedArray<ShadeLightCandidate>& lights);

	/// Moves the levels to `center`, then traces and blends this frame's probes
	void Update(CommandBuffer& cmd, const FVec3& center, const Settings& settings);

	/// Overwrites the lit target with a view of the probes
	void RenderDebug(CommandBuffer& cmd, const BaseCamera& camera, eDebugView view);

	bool IsReady() const { return mbImagesInitialized; }

	Image* GetIrradianceImage(uint32 level) { return mpIrradiance[level]; }
	Image* GetMomentsImage(uint32 level) { return mpMoments[level]; }
	RawGpuBuffer* GetStateBuffer() { return &mState; }
	RawGpuBuffer* GetProbeDataBuffer() { return &mProbeData; }
	RawGpuBuffer* GetForwardDataBuffer() { return &mForwardData; }

	uint32 GetStateFrameOffset(uint32 frame) const { return static_cast<uint32>(frame * scStateFrameSize); }
	uint32 GetForwardDataFrameOffset(uint32 frame) const { return static_cast<uint32>(frame * scForwardDataFrameSize); }

private:
	struct Level
	{
		float32 Spacing = 1.0f;
		int32 Origin[3] = {};
		bool bPlaced = false;

		/// 0 unfilled, 1 filled, scPending traced for the first time this frame, scDead never traced
		SizedArray<uint8> Valid;
		uint32 Cursor = 0;
		uint32 InvalidCount = 0;

		/// Dirty updates left per slot, how many slots have any, and where the search for them goes on
		SizedArray<uint8> Dirty;
		uint32 DirtyCount = 0;
		uint32 DirtyCursor = 0;
		/// The update each slot was last given a job in, so one is never traced twice in a frame
		SizedArray<uint32> JobStamp;
	};

	struct TrackedLight
	{
		uint32 ID = 0;
		bool bDirectional = false;
		FVec3 Position = FVec3::sZero;
		float32 Radius = 0.0f;
		float32 Rotation[4] = {};
		float32 Intensity = 0.0f;
		uint32 Color = 0;
	};

	/// Takes the unfilled slots whose grid points the mask rules out off the list of probes to trace
	void ClassifyLevel(uint32 level_index);
	void SetDirty(Level& level, uint32 slot, uint8 value);
	void MarkLightDirty(const TrackedLight& light, float32 strength);
	uint32 ScheduleDirtyJobs(uint32 level_index, uint32* jobs, uint32 job_count, uint32 limit);

	void PlaceLevel(uint32 level_index, const FVec3& center);
	uint32 ScheduleJobs(uint32* jobs, const Settings& settings);
	void GetLevelConsts(ProbeClipmapLevelGpu* out_levels, const Settings& settings) const;
	void LogStats(uint32 frame, uint32 job_count);
	void InitializeImages(CommandBuffer& cmd);
	void WriteForwardData(uint32 frame, const Settings& settings);

private:
	Level mLevels[scLevelCount];

	Image* mpIrradiance[scLevelCount] = {};
	Image* mpMoments[scLevelCount] = {};
	bool mbImagesInitialized = false;

	RawGpuBuffer mJobs;
	RawGpuBuffer mRays;
	RawGpuBuffer mState;
	RawGpuBuffer mStats;
	/// Per grid point: offset off it and whether the probe is switched on, written by the blend
	RawGpuBuffer mProbeData;
	/// Set by Reset, so the next update forgets the previous map's offsets
	bool mbClearProbeData = true;
	/// What the forward pass needs to read the probes, see ProbeClipmapForwardData
	RawGpuBuffer mForwardData;

	CompiledMapProbeMask mMask;
	bool mbMaskInUse = true;

	SizedArray<TrackedLight> mTrackedLights;
	bool mbHasTrackedLights = false;

	uint32 mUpdateCount = 0;
	uint32 mLastJobCount = 0;
	uint32 mLastDirtyJobCount = 0;
};

} // namespace fx::renderer
