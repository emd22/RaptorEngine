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

/// L2 SH irradiance of one probe. Mirrors ProbeSHData in Shaders/ProbeCommon.hlsli.
struct ProbeSHData
{
	/// Coefficients in the order of EvalProbeIrradiance(), already convolved with the cosine lobe so evaluating them
	/// gives irradiance / pi. Only .rgb is used, .a is padding.
	float32 SH[Limits::ProbeSHCoeffCount][4];
};

static_assert(sizeof(ProbeSHData) == Limits::ProbeSHCoeffCount * 4 * sizeof(float32),
			  "ProbeSHData must be tightly packed as 9 float4s to mirror the HLSL struct");

/// Placement of the probe grid, for the shader's trilinear lookup. Mirrors ProbeVolume in Shaders/ProbeCommon.hlsli.
struct ProbeVolumeData
{
	float32 Min[4];
	float32 InvCellSize[4];
	/// Grid dimensions in XYZ, probe count in W
	uint32 DimsAndCount[4];
};

static_assert(sizeof(ProbeVolumeData) == 48, "ProbeVolumeData must mirror the HLSL ProbeVolume struct");

/// Where a probe ended up after placement, and its depth moments for visibility. Mirrors ProbeInfo in
/// Shaders/ProbeCommon.hlsli.
struct ProbeInfo
{
	float32 ProbePosition[4];
	/// Mean distance and mean squared distance for each texel of a 6 face cubemap
	float32 DepthMoments[Limits::ProbeDepthFloatCount];
};

static_assert(sizeof(ProbeInfo) == Limits::ProbeDepthFloatCount * sizeof(float32) + sizeof(float32) * 4,
			  "ProbeInfo must be tightly packed to mirror the HLSL struct");

/// World space boxes of the level's geometry, used to fit the probe volume and keep probes out of walls
struct ProbePlacementBoxes;

/**
 * @brief A grid of L2 SH irradiance probes, with depth moments so that probes behind walls don't light a surface.
 *
 * Probes are baked by rendering a cubemap at each probe's position, a few probes per frame. The results live on the GPU
 * in `GraphicsBackend::ProbeBuffer`, `ProbeVolumeBuffer` and `ProbeDepthBuffer`, and can be cached to a file per scene.
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

	uint32 GetProbeCount() const { return Limits::MaxIrradianceProbes; }
	Vec3f GetProbePosition(uint32 index) const;

	/// The next probe to be captured by a bake
	uint32 GetCurrentProbeIndex() const { return mCurrentProbe; }

	///////////////////////////////////
	// Baking
	///////////////////////////////////

	/// Fits the probe grid to the level's geometry and starts baking it.
	void BeginGridBake();

	/// Starts baking a grid of `size` centred on `center`, instead of fitting it to the level.
	void BeginGridBakeAt(const Vec3f& center, const Vec3f& size);

	bool IsBaking() const { return mBakeState != eBakeState::Idle; }

	/// A batch of probes is waiting to be captured this frame, see RecordCaptureBatch().
	bool IsCapturePending() const { return mBakeState == eBakeState::CapturePending; }

	/// True only while the capture faces are being drawn. The main view keeps its probe GI and SSAO during a bake.
	bool IsCapturingFaces() const { return mbCapturingFaces; }

	/// Records the capture faces for the next batch of probes into `cmd`, drawing each face with `render_face`.
	void RecordCaptureBatch(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face);

	/// Reads back the batch captured this frame and arms the next one. Call after the frame has been submitted.
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

	void StartBake(const Vec3f& volume_min, const Vec3f& volume_size, const ProbePlacementBoxes& boxes);
	void PlaceGridProbes(const Vec3f& volume_min, const Vec3f& volume_size, const ProbePlacementBoxes& boxes);

	void CreateCaptureResources();
	void BuildCaptureTexels();
	void CopyTargetToStaging(renderer::CommandBuffer& cmd, eImageFormat format, renderer::RawGpuBuffer& staging);

	bool ReadBackProbe(uint32 batch_slot, uint32 probe_index);

	/// Projects one probe's captured faces (RGBA16F colour and reverse-Z depth, one pointer per face) into its SH and
	/// depth moments.
	void ProjectCapture(const uint16* const colors[scCaptureFaces], const float32* const depths[scCaptureFaces],
						ProbeSHData& out_sh, ProbeInfo& out_info) const;

	/// Copies the volume, and the SH and depth moments of probes [`first_probe`, `first_probe + count`) to the GPU.
	void UploadToGpu(uint32 first_probe, uint32 count);

private:
	ProbeSHData mProbes[Limits::MaxIrradianceProbes] {};
	ProbeInfo mProbeInfos[Limits::MaxIrradianceProbes] {};
	ProbeVolumeData mVolume {};

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
