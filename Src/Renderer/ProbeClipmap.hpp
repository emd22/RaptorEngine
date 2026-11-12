/*
 * File:        ProbeClipmap.hpp
 * Author:      emd22
 * Created:     10/11/2026
 * Description: Runtime GI probes in camera centred toroidal levels, traced against the SDF pages. A fixed budget of
 *              probes is traced each frame and blended into octahedral irradiance and depth moment tiles. They are
 *              the only indirect light: the forward pass, the fog and the particles all read them.
 */

#pragma once

#include "Backend/GpuBuffer.hpp"

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Map/CompiledMap.hpp>
#include <Math/FVec3.hpp>
#include <Math/MathConsts.hpp>

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
	/// The point the window is centred on, in probes
	float32 Center[4] = {};
};

/// Laid out to match ClipmapProxy in Shaders/ProbeClipmapConsts.hlsli
struct ProbeClipmapProxyGpu
{
	/// The box's unit axes, each with W = -dot(axis, centre), so a point's local coordinates are dot(axis, p) + W
	float32 Axes[3][4] = {};
	/// Half extents along the axes, then the albedo packed as RGB8
	float32 Extents[4] = {};
};

/**
 * @brief A box the probes trace besides the SDF pages, for a mesh of an object tagged DynamicGI. Its axes are unit
 * length and orthogonal, and `HalfExtents` is in world units.
 */
struct DynamicGIProxy
{
	/// The object the box was made from, to follow it from frame to frame
	uint32 ID = 0;
	FVec3 Center = FVec3::sZero;
	FVec3 Axes[3] = { FVec3::sRight, FVec3::sUp, FVec3::sForward };
	FVec3 HalfExtents = FVec3::sZero;
	FVec3 Albedo = FVec3(0.5f);
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
	static constexpr uint32 scMomentsSize = 16;
	/// All levels share one atlas per kind, the levels stacked down it
	static constexpr uint32 scTileColumns = scGridX * scGridY;
	static constexpr uint32 scTileRows = scGridZ * scLevelCount;

	/// How many of its next updates a fully dirty probe is traced first and blended faster for. Mirrors
	/// CLIPMAP_DIRTY_UPDATES in Shaders/ProbeClipmapConsts.hlsli, and has to fit the job's four dirty bits.
	static constexpr uint32 scDirtyUpdates = 8;

	/// Probe spacing per level. Level 0 is dense and close around the camera, and level 1 bridges it to the coarse
	/// level 2. Mirrored by scProbeMaskSpacings in Tools/MapCompiler/Compiler.cpp.
	static constexpr float32 scLevelSpacings[scLevelCount] = { 0.5f, 1.0f, 4.0f };
	/// Mirrors CLIPMAP_MAX_PROXIES in Shaders/ProbeClipmapConsts.hlsli
	static constexpr uint32 scMaxProxies = 32;

	/// Screen probes: one per tile of this many pixels, at most this many, each this many float4s. Mirror
	/// Shaders/ScreenProbeCommon.hlsli.
	static constexpr uint32 scScreenProbeTile = 16;
	static constexpr uint32 scMaxScreenProbes = 65536;
	static constexpr uint32 scScreenProbeStride = 5;
	static constexpr uint32 scMaxJobsPerFrame = 8192;

	/// One uint4 record per slot, written by the blend: the probe it holds, whether it is filled and switched on, its
	/// offset, age and back face share. See bProbes in Shaders/ProbeClipmapCommon.hlsli.
	static constexpr uint64 scProbesSize = static_cast<uint64>(scLevelCount) * scProbesPerLevel * sizeof(uint32) * 4;
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
		Backfaces,
		Backfaces1,
		Backfaces2,
		Weight,
		ScreenProbes,
		ScreenProbeCoverage,
		Count,
	};

	struct Settings
	{
		/// Probes traced per frame
		uint32 Budget = 3072;

		float32 RayLength = 30.0f;

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

		bool bBounce = true;
		bool bRelocate = true;
		bool bUseMask = true;

		float32 SkyRadiance = 1500.0f / FX_PI;
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

	/**
	 * @brief Sets the boxes of the moving objects that the next updates trace, up to scMaxProxies. With `track`, the
	 * probes around the boxes that appeared, went or moved since the last call are marked dirty.
	 */
	void SetDynamicProxies(const SizedArray<DynamicGIProxy>& proxies, bool track);

	/// Moves the levels to `center`, then traces and blends this frame's probes
	void Update(CommandBuffer& cmd, const FVec3& center, const Settings& settings);

	/// Overwrites the lit target with a view of the probes
	void RenderDebug(CommandBuffer& cmd, const BaseCamera& camera, eDebugView view);

	/**
	 * Places a probe on the surface of each screen tile and traces it, for the GI resolve to read. Needs this frame's
	 * prepass and the update's trace constants.
	 */
	void TraceScreenProbes(CommandBuffer& cmd, const BaseCamera& camera, UVec2 extent);

	bool IsReady() const { return mbImagesInitialized; }

	/// Screen probe settings: metres traced through the SDF pages before the clipmap is read, the cone slope, and how
	/// many times the trace is dispatched (for timing)
	float32 ScreenProbeRayLength = 2.0f;
	float32 ScreenProbeCone = 0.0f;
	uint32 ScreenProbeRepeat = 1;

	Image* GetIrradianceImage() { return mpIrradiance; }
	Image* GetMomentsImage() { return mpMoments; }
	RawGpuBuffer* GetProbeBuffer() { return &mProbes; }
	RawGpuBuffer* GetForwardDataBuffer() { return &mForwardData; }
	RawGpuBuffer* GetScreenProbeBuffer() { return &mScreenProbes; }

	uint32 GetForwardDataFrameOffset(uint32 frame) const { return static_cast<uint32>(frame * scForwardDataFrameSize); }

private:
	struct Level
	{
		float32 Spacing = 1.0f;
		int32 Origin[3] = {};
		bool bPlaced = false;

		/// 0 unfilled, 1 filled, scPending traced for the first time this frame, scDead never traced. The GPU keeps its
		/// own copy in the probe records, which is what the passes reading the probes go by.
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
	void InitializeImages(CommandBuffer& cmd);

	void WriteForwardData(uint32 frame, const Settings& settings, float32 fallback);

private:
	Level mLevels[scLevelCount];

	Image* mpIrradiance = nullptr;
	Image* mpMoments = nullptr;
	bool mbImagesInitialized = false;

	RawGpuBuffer mJobs;
	RawGpuBuffer mRays;
	/// Per slot: the probe it holds and its state, written by the blend
	RawGpuBuffer mProbes;
	/// Set by Reset, so the next update forgets the previous map's probes
	bool mbClearProbes = true;
	FVec3 mCenter = FVec3::sZero;
	/// What the forward pass needs to read the probes, see ProbeClipmapForwardData
	RawGpuBuffer mForwardData;
	/// Screen probes, see scScreenProbeStride
	RawGpuBuffer mScreenProbes;

	CompiledMapProbeMask mMask;
	/// The mask level each level reads, the one built for its spacing, or -1 where the map has none
	int32 mMaskLevels[scLevelCount] = { -1, -1, -1 };
	bool mbMaskInUse = true;

	SizedArray<TrackedLight> mTrackedLights;
	bool mbHasTrackedLights = false;

	/// This frame's boxes, and the last ones the dirty tracking compared against
	SizedArray<DynamicGIProxy> mProxies;
	SizedArray<DynamicGIProxy> mTrackedProxies;
	bool mbHasTrackedProxies = false;

	uint32 mUpdateCount = 0;
	uint32 mLastJobCount = 0;
	uint32 mLastDirtyJobCount = 0;
};

} // namespace fx::renderer
