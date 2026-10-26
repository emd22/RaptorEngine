#pragma once

#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Backend/Commands.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Renderer/Camera/PerspectiveCamera.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/ProbePlacement.hpp>
#include <Renderer/RenderStage.hpp>
#include <functional>

namespace fx {

/// L2 SH irradiance, cosine convolved. Mirrors ProbeSHData in Shaders/ProbeCommon.hlsli.
struct ProbeSHData
{
	float32 SH[Limits::ProbeSHCoeffCount][4];
};

static_assert(sizeof(ProbeSHData) == Limits::ProbeSHCoeffCount * 4 * sizeof(float32));

struct ProbeInfo
{
	float32 ProbePosition[4];
	/// Mean distance and mean squared distance per texel
	float32 DepthMoments[Limits::ProbeDepthFloatCount];
};

static_assert(sizeof(ProbeInfo) == (Limits::ProbeDepthFloatCount + 4) * sizeof(float32));

struct ReflectionProbeData
{
	float32 WorldToBox[16];
	float32 PositionAndCount[4];
	float32 Fade[4];
};

static_assert(sizeof(ReflectionProbeData) == 96);

class ProbeManager
{
public:
	static constexpr uint32 scCaptureSize = 64;
	static constexpr uint32 scCaptureFaces = 6;
	static constexpr uint32 scProbesPerFrame = 4;

	using RenderFaceFunc = std::function<void(PerspectiveCamera2& camera, renderer::RenderStage& stage)>;

public:
	void Create();
	void Destroy();

	uint32 GetProbeCount() const { return mLayout.GetProbeCount(); }
	Vec3f GetProbePosition(uint32 index) const { return mLayout.Positions[index]; }
	uint32 GetCurrentProbeIndex() const { return (mBakePhase == eBakePhase::Irradiance) ? mCurrentProbe : UINT32_MAX; }

	uint32 GetVolumeCount() const { return mLayout.VolumeCount; }

	bool SampleIrradiance(const Vec3f position, const Vec3f normal, Vec3f& out_irradiance) const;
	void GetVolumeProbeRange(uint32 volume, uint32& out_first_probe, uint32& out_count) const;

	uint32 RebuildVolumesFromWorld();

	void SetSunVisibility(Slice<const Vec3f> positions, Slice<const uint8> values);
	void ClearSunVisibility();

	uint32 GetReflectionProbeCount() const { return mReflectionProbeCount; }
	Vec3f GetReflectionProbePosition(uint32 index) const;
	bool AreReflectionsBaked() const { return mbReflectionsBaked; }

	uint32 RebuildReflectionProbesFromWorld();

	void BeginBake();
	void BeginReflectionBake();

	bool IsBaking() const { return mBakeState != eBakeState::Idle; }
	bool IsBakingReflections() const { return IsBaking() && mBakePhase == eBakePhase::Reflection; }
	bool IsCapturePending() const { return mBakeState == eBakeState::CapturePending; }
	bool IsCapturingFaces() const { return mbCapturingFaces; }
	bool IsCapturingReflection() const { return mbCapturingFaces && mBakePhase == eBakePhase::Reflection; }
	bool IsCapturingBounce() const { return mbCapturingFaces && (mCurrentBounce > 0 || IsCapturingReflection()); }

	Vec2u GetCaptureExtent() const
	{
		const uint32 size = (mBakePhase == eBakePhase::Reflection) ? Limits::ReflectionProbeSize : scCaptureSize;
		return Vec2u(size, size);
	}

	void RecordCaptureBatch(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face);
	void ServiceCaptureBake();
	void ServiceStaleCheck();

	bool SaveProbes();
	bool LoadProbes();

private:
	enum class eBakeState
	{
		Idle,
		CapturePending,
		CaptureRecorded,
	};

	enum class eBakePhase
	{
		Irradiance,
		Reflection,
	};

	struct CaptureTexel
	{
		float32 WeightedBasis[Limits::ProbeSHCoeffCount];
		float32 SolidAngle;
		uint32 MomentTexel;
	};

	void ApplyLayout();
	bool BuildLayoutFromWorld(ProbeLayout& out_layout, uint32& out_brush_volumes) const;

	uint32 SelectVolume(const Vec3f position) const;
	bool SampleVolume(const Vec3f position, uint32 volume_index, const float32* basis, Vec3f& out_irradiance) const;
	void ResolveSunVisibility();

	void CreateCaptureResources();
	void BuildCaptureTexels();
	void CopyTargetToStaging(renderer::CommandBuffer& cmd, renderer::RenderStage& stage, uint32 size,
							 eImageFormat format, renderer::RawGpuBuffer& staging);

	bool ReadBackProbe(uint32 batch_slot, uint32 probe_index);
	void ProjectCapture(const uint16* const colors[scCaptureFaces], const float32* const depths[scCaptureFaces],
						ProbeSHData& out_sh, ProbeInfo& out_info) const;

	void UploadToGpu(uint32 first_probe, uint32 count);
	void UploadMomentsAtlas(uint32 first_probe, uint32 count);

	void StartReflectionPhase();
	void RecordReflectionCapture(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face);
	void ServiceReflectionBake();
	bool ReadBackReflectionProbe(uint32 probe_index);
	void UploadReflectionProbes(uint32 visible_count);
	void UploadReflectionCubemap(uint32 probe_index);

	bool SaveIrradianceProbes();
	bool LoadIrradianceProbes();
	bool LoadCompiledProbes();
	bool SaveReflectionProbes();
	bool LoadReflectionProbes();

	/**
	 * @brief Create all cameras that probes are captured with
	 */
	void MakeCaptureCameras();

	void MoveCaptureCameras(const Vec3f position);

private:
	ProbeLayout mLayout;
	ProbeSHData mProbes[Limits::MaxIrradianceProbes] {};
	ProbeInfo mProbeInfos[Limits::MaxIrradianceProbes] {};
	bool mbGridDirty = true;

	static constexpr uint32 scStaleCheckMaxWaitFrames = 600;
	bool mbStaleCheckPending = false;
	uint32 mStaleCheckWaitFrames = 0;

	DynArray<Vec3f> mSunSamplePositions;
	DynArray<uint8> mSunSampleValues;
	float32 mSunVisibility[Limits::MaxIrradianceProbes] {};
	bool mbSunVisibilityDirty = true;

	eBakeState mBakeState = eBakeState::Idle;
	eBakePhase mBakePhase = eBakePhase::Irradiance;

	bool mbCapturingFaces = false;

	uint32 mCurrentProbe = 0;
	uint32 mBatchStart = 0;
	uint32 mCurrentBounce = 0;
	uint32 mBounceCount = 1;

	ReflectionProbeData mReflectionProbes[Limits::MaxReflectionProbes] {};
	uint32 mReflectionProbeCount = 0;
	bool mbReflectionsBaked = false;
	SizedArray<uint16> mReflectionTexels;

	bool mbCaptureResourcesCreated = false;
	renderer::RenderStage mCaptureStage;
	renderer::RawGpuBuffer mColorStaging[scProbesPerFrame][scCaptureFaces];
	renderer::RawGpuBuffer mDepthStaging[scProbesPerFrame][scCaptureFaces];
	SizedArray<CaptureTexel> mCaptureTexels;
	Mat4f mCaptureInvProjection = Mat4f::scIdentity;
	Mat4f mCaptureFaceToClip[scCaptureFaces];


	SizedArray<PerspectiveCamera2> mCaptureCameras;

	renderer::RenderStage mReflectionStage;
	renderer::RawGpuBuffer mReflectionStaging[scCaptureFaces];
};

extern ProbeManager* gProbeManager;

} // namespace fx
