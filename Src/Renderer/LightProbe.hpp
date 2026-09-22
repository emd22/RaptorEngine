#pragma once

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Backend/Commands.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/RenderStage.hpp>
#include <functional>

namespace fx {

/// L2 SH irradiance of one probe
struct ProbeSHData
{
	/// Coefficients in the order of EvalProbeIrradiance(), already convolved with the cosine lobe so evaluating them
	/// gives irradiance / pi. Only .rgb is used, .a is padding.
	float32 SH[Limits::ProbeSHCoeffCount][4];
};

static_assert(sizeof(ProbeSHData) == Limits::ProbeSHCoeffCount * 4 * sizeof(float32),
			  "ProbeSHData must be tightly packed as 9 float4s to mirror the HLSL struct");

struct ProbeVolumeData
{
	/// World space min corner in XYZ. W holds the number of volumes in the buffer.
	float32 MinAndCount[4];

	float32 InvCellSize[4];

	/// Grid dimensions in XYZ, and in W the index of this volume's first probe in the probe buffers
	uint32 DimsAndFirst[4];

	float32 MaxAndCellVolume[4];
};

static_assert(sizeof(ProbeVolumeData) == 64, "ProbeVolumeData must mirror the HLSL ProbeVolume struct");

/// Probe grid resolution of one volume, in probes along each axis
struct ProbeGridSize
{
	uint32 X = Limits::ProbeGridDims[0];
	uint32 Y = Limits::ProbeGridDims[1];
	uint32 Z = Limits::ProbeGridDims[2];

	uint32 GetProbeCount() const { return X * Y * Z; }

	bool IsValid() const
	{
		return X >= Limits::MinProbeGridDim && Y >= Limits::MinProbeGridDim && Z >= Limits::MinProbeGridDim;
	}
};

/// Where a probe ended up after placement, and its depth moments for visibility. Mirrors ProbeInfo in
/// Shaders/ProbeCommon.hlsli.
struct ProbeInfo
{
	/// World space position in XYZ. W is 1 for an active probe, 0 for one that placement couldn't put anywhere useful.
	float32 ProbePosition[4];
	/// Mean distance and mean squared distance for each texel of a 6 face cubemap
	float32 DepthMoments[Limits::ProbeDepthFloatCount];
};

static_assert(sizeof(ProbeInfo) == Limits::ProbeDepthFloatCount * sizeof(float32) + sizeof(float32) * 4,
			  "ProbeInfo must be tightly packed to mirror the HLSL struct");

struct ProbePlacementBoxes;

/**
 * @brief Grids of L2 SH irradiance probes, with depth moments so that probes behind walls don't light a surface.
 */
class ProbeManager
{
public:
	/// Resolution of each capture cubemap face
	static constexpr uint32 scCaptureSize = 64;
	static constexpr uint32 scCaptureFaces = 6;

	/// Number of probes captured per frame during a bake
	static constexpr uint32 scProbesPerFrame = 4;

	/// Draws the scene from `camera` into `stage`, including any work needed before the render pass begins.
	using RenderFaceFunc = std::function<void(PerspectiveCamera& camera, renderer::RenderStage& stage)>;

public:
	void Create();
	void Destroy();

	uint32 GetProbeCount() const { return mProbeCount; }
	Vec3f GetProbePosition(uint32 index) const;
	bool IsProbeActive(uint32 index) const { return mProbeInfos[index].ProbePosition[3] > 0.5f; }

	uint32 GetCurrentProbeIndex() const { return mCurrentProbe; }

	///////////////////////////////////
	// Volumes
	///////////////////////////////////

	uint32 GetVolumeCount() const { return mVolumeCount; }
	uint32 GetVolumeOfProbe(uint32 probe_index) const;
	void GetVolumeProbeRange(uint32 volume, uint32& out_first_probe, uint32& out_count) const;

	void ClearVolumes();
	bool AddVolume(const Vec3f& center, const Vec3f& size, const ProbeGridSize& grid = {});
	bool AddLevelVolume(const ProbeGridSize& grid = {});

	uint32 RebuildVolumesFromWorld();

	///////////////////////////////////
	// Baking
	///////////////////////////////////

	void BeginBake();
	void BeginGridBake();
	void BeginGridBakeAt(const Vec3f& center, const Vec3f& size, const ProbeGridSize& grid = {});

	bool IsBaking() const { return mBakeState != eBakeState::Idle; }
	bool IsCapturePending() const { return mBakeState == eBakeState::CapturePending; }

	/// True only while the capture faces are being drawn. The main view keeps its probe GI and SSAO during a bake.
	bool IsCapturingFaces() const { return mbCapturingFaces; }
	void RecordCaptureBatch(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face);
	void ServiceCaptureBake();

	///////////////////////////////////
	// Probe cache
	///////////////////////////////////

	/// Saves the probes for the current scene. They are picked up by LoadProbes() on the next run.
	bool SaveProbes();
	bool LoadProbes();

private:
	enum class eBakeState
	{
		Idle,
		/// A batch will be captured this frame
		CapturePending,
		/// A batch was recorded this frame and is waiting to be read back
		CaptureRecorded,
	};

	/// Everything about a capture texel that is the same for every probe
	struct CaptureTexel
	{
		/// SH basis of the texel's direction, multiplied by the solid angle it covers
		float32 WeightedBasis[Limits::ProbeSHCoeffCount];
		float32 SolidAngle;
		/// The depth moments texel that the texel's direction falls in
		uint32 MomentTexel;
	};


	bool AddVolumeAndPlaceProbes(const Vec3f& volume_min, const Vec3f& volume_size, const ProbeGridSize& grid,
								 const ProbePlacementBoxes& boxes);
	void StartSingleVolumeBake(const Vec3f& volume_min, const Vec3f& volume_size, const ProbeGridSize& grid,
							   const ProbePlacementBoxes& boxes);
	void PlaceVolumeProbes(uint32 volume_index, uint32 first_probe, const Vec3f& volume_min, const Vec3f& volume_size,
						   const ProbeGridSize& grid, const ProbePlacementBoxes& boxes);
	void RefreshVolumeCounts();

	void CreateCaptureResources();
	void BuildCaptureTexels();
	void CopyTargetToStaging(renderer::CommandBuffer& cmd, eImageFormat format, renderer::RawGpuBuffer& staging);

	bool ReadBackProbe(uint32 batch_slot, uint32 probe_index);
	void ProjectCapture(const uint16* const colors[scCaptureFaces], const float32* const depths[scCaptureFaces],
						ProbeSHData& out_sh, ProbeInfo& out_info) const;

	/// Copies the volumes, and the SH and depth moments of probes [`first_probe`, `first_probe + count`) to the GPU.
	void UploadToGpu(uint32 first_probe, uint32 count);

	void UploadMomentsAtlas(uint32 first_probe, uint32 count);

private:
	ProbeSHData mProbes[Limits::MaxIrradianceProbes] {};
	ProbeInfo mProbeInfos[Limits::MaxIrradianceProbes] {};

	ProbeVolumeData mVolumes[Limits::MaxProbeVolumes] {};
	uint32 mVolumeCount = 0;

	/// Probes taken by `mVolumes`, which is where the next volume's range starts
	uint32 mProbeCount = 0;

	eBakeState mBakeState = eBakeState::Idle;
	bool mbCapturingFaces = false;
	uint32 mCurrentProbe = 0;
	uint32 mBatchStart = 0;

	// Capture resources, created by the first bake

	bool mbCaptureResourcesCreated = false;
	renderer::RenderStage mCaptureStage;
	renderer::RawGpuBuffer mColorStaging[scProbesPerFrame][scCaptureFaces];
	renderer::RawGpuBuffer mDepthStaging[scProbesPerFrame][scCaptureFaces];
	SizedArray<CaptureTexel> mCaptureTexels;
	/// Inverse projection shared by every capture face
	Mat4f mCaptureInvProjection = Mat4f::scIdentity;
};

extern ProbeManager* gProbeManager;

} // namespace fx
