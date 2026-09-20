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

/// Placement of one probe grid, for the shader's trilinear lookup. Mirrors ProbeVolume in
/// Shaders/ProbeCommon.hlsli.
struct ProbeVolumeData
{
	/// World space min corner in XYZ. W holds the number of volumes in the buffer, the same in every entry, so
	/// that the shader can read the list's length out of any one of them.
	float32 MinAndCount[4];

	float32 InvCellSize[4];

	/// Grid dimensions in XYZ, and in W the index of this volume's first probe in the probe buffers
	uint32 DimsAndFirst[4];
};

static_assert(sizeof(ProbeVolumeData) == 48, "ProbeVolumeData must mirror the HLSL ProbeVolume struct");

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
	float32 ProbePosition[4];
	/// Mean distance and mean squared distance for each texel of a 6 face cubemap
	float32 DepthMoments[Limits::ProbeDepthFloatCount];
};

static_assert(sizeof(ProbeInfo) == Limits::ProbeDepthFloatCount * sizeof(float32) + sizeof(float32) * 4,
			  "ProbeInfo must be tightly packed to mirror the HLSL struct");

/// World space boxes of the level's geometry, used to fit the probe volume and keep probes out of walls
struct ProbePlacementBoxes;

/**
 * @brief Grids of L2 SH irradiance probes, with depth moments so that probes behind walls don't light a surface.
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

	/// Probes placed across every volume, which is what the bake walks
	uint32 GetProbeCount() const { return mProbeCount; }
	Vec3f GetProbePosition(uint32 index) const;

	/// The next probe to be captured by a bake
	uint32 GetCurrentProbeIndex() const { return mCurrentProbe; }

	///////////////////////////////////
	// Volumes
	///////////////////////////////////

	uint32 GetVolumeCount() const { return mVolumeCount; }

	/// Index of the volume that owns probe `probe_index`, or `Limits::MaxProbeVolumes` if no volume does
	uint32 GetVolumeOfProbe(uint32 probe_index) const;

	/// Index of `volume`'s first probe, and how many it has
	void GetVolumeProbeRange(uint32 volume, uint32& out_first_probe, uint32& out_count) const;

	/// Removes every volume. The probes they placed keep their baked lighting, but nothing samples them until a
	/// volume covers them again.
	void ClearVolumes();

	/// Adds a volume of `size` centred on `center` and places its probes, leaving them unbaked. Returns false if
	/// `grid` is degenerate, or if there is no room left in the volume list or the probe budget.
	bool AddVolume(const Vec3f& center, const Vec3f& size, const ProbeGridSize& grid = {});

	/// Adds a volume fitted to the level's geometry, the way BeginGridBake() does. Returns false if there is no
	/// geometry to fit to, or if AddVolume() would have failed.
	bool AddLevelVolume(const ProbeGridSize& grid = {});

	/**
	 * @brief Replaces every volume with the ones the level defines: a coarse volume fitted to the whole level,
	 * plus a denser one for each brush the editor has tagged `eObjectTag::ProbeVolume`.
	 *
	 * Brush volumes take their grid from the `r_probe_spacing` cvar (metres between probes), coarsened as needed
	 * to fit what is left of the probe budget. Returns the number of brush volumes added.
	 */
	uint32 RebuildVolumesFromWorld();

	///////////////////////////////////
	// Baking
	///////////////////////////////////

	/// Starts baking every probe of every placed volume. Does nothing if no volume has been added.
	void BeginBake();

	/// Replaces every volume with one fitted to the level's geometry, and starts baking it.
	void BeginGridBake();

	/// Replaces every volume with a grid of `size` centred on `center`, and starts baking it.
	void BeginGridBakeAt(const Vec3f& center, const Vec3f& size, const ProbeGridSize& grid = {});

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

	/// Projects one probe's captured faces (RGBA16F colour and reverse-Z depth, one pointer per face) into its SH and
	/// depth moments.
	void ProjectCapture(const uint16* const colors[scCaptureFaces], const float32* const depths[scCaptureFaces],
						ProbeSHData& out_sh, ProbeInfo& out_info) const;

	/// Copies the volumes, and the SH and depth moments of probes [`first_probe`, `first_probe + count`) to the GPU.
	void UploadToGpu(uint32 first_probe, uint32 count);

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
