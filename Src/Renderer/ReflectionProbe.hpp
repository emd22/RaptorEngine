/*
 * File:        ReflectionProbe.hpp
 * Author:      emd22
 * Created:     10/09/2026
 * Description: Reflection probes: box brushes whose surroundings are captured into prefiltered cubemaps
 */

#pragma once

#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/FVec3.hpp>
#include <Math/Float16.hpp>
#include <Renderer/Backend/Commands.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Renderer/Camera/PerspectiveCamera.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/RenderStage.hpp>
#include <functional>

namespace fx {

struct ReflectionProbeData
{
	float32 WorldToBox[16];
	float32 PositionAndCount[4];
	float32 Fade[4];
};

static_assert(sizeof(ReflectionProbeData) == 96);

/**
 * @brief A reflection probe as is transferred to the GPU
 */
struct ReflectionProbeGpu
{
	ReflectionProbeData Probe;

	/// Luminance of the irradiance at the capture point (along +X +Y +Z), w if it is known. All zero until the probe
	/// is baked.
	float32 CaptureIrradiancePositive[4];

	/// The same along -X -Y -Z
	float32 CaptureIrradianceNegative[4];
};

static_assert(sizeof(ReflectionProbeGpu) == 128);

class ReflectionProbeManager
{
public:
	static constexpr uint32 scCaptureFaces = 6;

	/// Frames the GI probes are given to fill in around a probe before it is captured
	static constexpr uint32 scSettleFrames = 64;

	using RenderFaceFunc = std::function<void(PerspectiveCamera& camera, renderer::RenderStage& stage)>;

public:
	void Create();
	void Destroy();

	uint32 GetReflectionProbeCount() const { return mReflectionProbeCount; }
	FVec3 GetReflectionProbePosition(uint32 index) const;
	bool AreReflectionsBaked() const { return mbReflectionsBaked; }

	uint32 RebuildReflectionProbesFromWorld();

	void BeginBake();

	bool IsBaking() const { return mBakeState != eBakeState::Idle; }
	bool IsCapturePending() const { return mBakeState == eBakeState::CapturePending && mSettleFrames == 0; }
	bool IsCapturingFaces() const { return mbCapturingFaces; }

	/// Where the next capture is taken from. The GI probes are centred on it while the bake runs, since they are what
	/// lights the capture.
	FVec3 GetCapturePosition() const;

	UVec2 GetCaptureExtent() const { return UVec2(Limits::ReflectionProbeSize, Limits::ReflectionProbeSize); }

	void RecordCapture(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face);
	void ServiceBake();

	bool SaveProbes();
	bool LoadProbes();

private:
	enum class eBakeState
	{
		Idle,
		CapturePending,
		CaptureRecorded,
	};

	void CreateCaptureResources();
	void CopyTargetToStaging(renderer::CommandBuffer& cmd, renderer::RenderStage& stage, uint32 size,
							 eImageFormat format, renderer::RawGpuBuffer& staging);

	bool ReadBackReflectionProbe(uint32 probe_index);
	void UploadReflectionProbes(uint32 visible_count);
	void GatherCaptureIrradiance(uint32 probe_index, ReflectionProbeGpu& probe) const;
	void UploadReflectionCubemap(uint32 probe_index);

	bool LoadReflectionProbes();

	void MakeCaptureCameras();
	void MoveCaptureCameras(const FVec3 position);

private:
	eBakeState mBakeState = eBakeState::Idle;
	bool mbCapturingFaces = false;
	uint32 mCurrentProbe = 0;
	uint32 mSettleFrames = 0;

	ReflectionProbeData mReflectionProbes[Limits::MaxReflectionProbes] {};
	uint32 mReflectionProbeCount = 0;
	bool mbReflectionsBaked = false;
	SizedArray<Float16> mReflectionTexels;

	bool mbCaptureResourcesCreated = false;
	FMat4 mCaptureFaceToClip[scCaptureFaces];
	SizedArray<PerspectiveCamera> mCaptureCameras;

	renderer::RenderStage mStage;
	renderer::RawGpuBuffer mStaging[scCaptureFaces];
};

extern ReflectionProbeManager* gReflectionProbes;

} // namespace fx
