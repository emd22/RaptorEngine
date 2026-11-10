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
#include <Math/FVec3.hpp>

namespace fx {
class Image;
class BaseCamera;
} // namespace fx

namespace fx::renderer {

class CommandBuffer;
struct Target;

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
	static constexpr uint32 scMomentsSize = 8;
	static constexpr uint32 scTileColumns = scGridX * scGridY;

	static constexpr float32 scLevel0Spacing = 1.0f;
	static constexpr uint32 scMaxJobsPerFrame = 8192;

	enum class eDebugView : uint32
	{
		None,
		Spheres0,
		Surface,
		Levels,
		Spheres1,
		Spheres2,
		Count,
	};

	struct Settings
	{
		/// Probes traced per frame
		uint32 Budget = 3072;
		float32 RayLengths[scLevelCount] = { 15.0f, 20.0f, 30.0f };
		float32 ConeSlope = 0.1f;
		float32 Hysteresis = 0.97f;
		/// Hysteresis of the texels whose light changed by more than ChangeThreshold, relative to before
		float32 FastHysteresis = 0.8f;
		float32 ChangeThreshold = 1.0f;
		/// Exponent of the cosine lobe each depth moment texel gathers over
		float32 MomentSharpness = 50.0f;
		/// Hits take the light that reached them from last frame's probes
		bool bBounce = true;
		/// Probes are moved out of walls and away from surfaces, and switched off where that fails
		bool bRelocate = true;
		/// Radiance of the rays that miss, the same constant sky the mapc bake uses
		float32 SkyRadiance = 1500.0f / 3.14159265f;
	};

public:
	void Create();
	void Destroy();
	void CreatePipelines(Target* depth, Target* normals, Target* lit);

	/// Forgets every probe, for a new map
	void Reset();

	/// Moves the levels to `center`, then traces and blends this frame's probes
	void Update(CommandBuffer& cmd, const FVec3& center, const Settings& settings);

	/// Overwrites the lit target with a view of the probes
	void RenderDebug(CommandBuffer& cmd, const BaseCamera& camera, eDebugView view);

	bool IsReady() const { return mbImagesInitialized; }

private:
	struct Level
	{
		float32 Spacing = 1.0f;
		int32 Origin[3] = {};
		bool bPlaced = false;

		/// 0 unfilled, 1 filled, scPending traced for the first time this frame
		SizedArray<uint8> Valid;
		uint32 Cursor = 0;
		uint32 InvalidCount = 0;
	};

	void PlaceLevel(uint32 level_index, const FVec3& center);
	uint32 ScheduleJobs(uint32* jobs, const Settings& settings);
	void GetLevelConsts(ProbeClipmapLevelGpu* out_levels, const Settings& settings) const;
	void LogStats(uint32 frame, uint32 job_count);
	void InitializeImages(CommandBuffer& cmd);

private:
	Level mLevels[scLevelCount];

	Image* mpIrradiance[scLevelCount] = {};
	Image* mpMoments[scLevelCount] = {};
	bool mbImagesInitialized = false;

	RawGpuBuffer mJobs;
	RawGpuBuffer mRays;
	RawGpuBuffer mState;
	RawGpuBuffer mStats;
	/// Offset off the grid point and whether the probe is switched on, written by the blend
	RawGpuBuffer mProbeData;

	uint32 mUpdateCount = 0;
	uint32 mLastJobCount = 0;
};

} // namespace fx::renderer
