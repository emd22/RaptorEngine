/*
 * File:        LightProbe.cpp
 * Author:      emd22
 * Created:     10/09/2026
 * Description: All light probe and light probe grid logic. Irradiance probes with baked L2 SH and depth moments for
 * visibility (VSM & DDGI-ish).
 */


#include "LightProbe.hpp"

#include <Asset/AssetManager.hpp>
#include <Blockout.hpp>
#include <CVar.hpp>
#include <Core/File.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/BarrierHelper.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/ReflectionFilter.hpp>
#include <Util/RustInterop.hpp>
#include <World.hpp>
#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

/// Bump whenever the layout or meaning of the cached data changes
#define FX_PROBE_CACHE_FILE_VERSION 12

namespace fx {
struct ProbePlacementBoxes
{
	ProbePlacementBoxes() : pBoxes(rx_placement_boxes_new()) {}

	ProbePlacementBoxes(const ProbePlacementBoxes&) = delete;
	ProbePlacementBoxes& operator=(const ProbePlacementBoxes&) = delete;

	ProbePlacementBoxes(ProbePlacementBoxes&& other) noexcept
		: pBoxes(other.pBoxes), Min(other.Min), Max(other.Max)
	{
		other.pBoxes = nullptr;
	}

	~ProbePlacementBoxes() { rx_placement_boxes_free(pBoxes); }

	void UpdateBounds() { rx_placement_boxes_bounds(pBoxes, &Min.mData[0], &Max.mData[0]); }

	bool IsEmpty() const { return rx_placement_boxes_is_empty(pBoxes); }

	RxPlacementBoxes* pBoxes = nullptr;

	Vec3f Min = Vec3f(std::numeric_limits<float32>::max());
	Vec3f Max = Vec3f(-std::numeric_limits<float32>::max());
};


static_assert(RX_PROBE_FACES == ProbeManager::scCaptureFaces);
static_assert(RX_PROBE_SH_FLOATS == Limits::ProbeSHCoeffCount * 4);
static_assert(RX_PROBE_MOMENT_FLOATS == Limits::ProbeDepthFloatCount);
static_assert(RX_PROBE_DEPTH_SIZE == Limits::ProbeDepthSize);
static_assert(RX_PROBE_DEPTH_MAX_DISTANCE == Limits::ProbeDepthMaxDistance);

namespace {

///////////////////////////////////
// Spherical harmonics
///////////////////////////////////

ProbeSHData MakeSkyGradientProbe(const float32 sky[3], const float32 ground[3])
{
	ProbeSHData probe {};
	rx_probe_sky_gradient(sky, ground, &probe.SH[0][0]);
	return probe;
}

///////////////////////////////////
// Capture
///////////////////////////////////

/// Brightest radiance a capture texel can contribute, so that a few very bright texels can't dominate a probe
constexpr float32 scRadianceClamp = 65000.0f;

constexpr int64 scDefaultProbeBounces = 3;
constexpr int64 scMaxProbeBounces = 8;

/// Surfaces closer than this to a probe are clipped out of its capture, so it sees straight through them
constexpr float32 scCaptureNearPlane = 0.1f;

PerspectiveCamera MakeCaptureCamera(const Vec3f& position, uint32 face)
{
	static const Vec3f scFaceDirections[ProbeManager::scCaptureFaces] = {
		Vec3f(1.0f, 0.0f, 0.0f),  Vec3f(-1.0f, 0.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f),
		Vec3f(0.0f, -1.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f),	Vec3f(0.0f, 0.0f, -1.0f),
	};

	static const Vec3f scFaceUps[ProbeManager::scCaptureFaces] = {
		Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f),
		Vec3f(0.0f, 0.0f, 1.0f), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f),
	};

	PerspectiveCamera camera(90.0f, 1.0f, 200.0f, scCaptureNearPlane);

	camera.Position = position;
	camera.ViewMatrix.LookAt(position, position + scFaceDirections[face], scFaceUps[face]);
	camera.UpdateCameraMatrix();

	return camera;
}

/// Decodes an IEEE half float. Subnormals flush to zero, and NaNs decode as zero so that a bad texel can't light a
/// probe.
float32 HalfToFloat(uint16 half)
{
	const uint32 exponent = (half >> 10) & 0x1F;
	const uint32 mantissa = half & 0x3FF;

	if (exponent == 0 || (exponent == 31 && mantissa != 0)) {
		return 0.0f;
	}

	uint32 bits = static_cast<uint32>(half & 0x8000) << 16;
	bits |= (exponent == 31) ? 0x7F800000 : (((exponent + 112) << 23) | (mantissa << 13));

	return std::bit_cast<float32>(bits);
}

///////////////////////////////////
// Placement
///////////////////////////////////

/// Objects bigger than this (the sky) are left out of placement so that they can't blow up the volume
constexpr float32 scMaxPlacementObjectSize = 100.0f;

constexpr float32 scDefaultProbeSpacing = 2.5f;
constexpr float32 scDefaultLevelProbeSpacing = 0.5f;
constexpr float32 scMaxProbeSpacing = 64.0f;
constexpr float32 scSurfaceSpacingStep = 1.25f;

constexpr float32 scBaseProbeSpacing = 2.0f;
constexpr uint32 scBaseProbeGridBudget = 512;


void GetObjectWorldBounds(Object& object, Vec3f& out_min, Vec3f& out_max)
{
	// OBB::FromLocalBounds() does the same per-corner transform this used to do inline
	const AABB world_bounds = object.GetWorldOBB().GetWorldAABB();

	out_min = world_bounds.Min;
	out_max = world_bounds.Max;
}

ProbePlacementBoxes GatherPlacementBoxes()
{
	static_assert(sizeof(Mat4f) == 16 * sizeof(float32));

	ProbePlacementBoxes boxes;
	uint32 num_objects = 0;
	uint32 num_boxes = 0;

	for (Object& object : gObjectManager->GetCache()) {
		// Only the level's lit geometry. Unlit objects (the sky) and anything not probe visible (the view model) don't
		// count.
		if (!object.pMesh.IsValid() || object.IsUnlit() || !object.IsProbeVisible()) {
			continue;
		}

		const AABB world_bounds = object.GetWorldOBB().GetWorldAABB();

		if ((world_bounds.Max - world_bounds.Min).Length() > scMaxPlacementObjectSize) {
			continue;
		}

		const Mat4f& local_to_world = object.GetWorldMatrix();
		const Mat4f world_to_local = local_to_world.Inverse();

		// Blockouts are brushes, whose bounds are the box around the whole hull. Use the hull itself when it isn't
		// a box
		const Brush* brush = (gWorld != nullptr && gWorld->pBlockout != nullptr)
								 ? gWorld->pBlockout->GetBrush(&object)
								 : nullptr;

		std::vector<float32> planes;

		if (brush != nullptr && brush->IsValid() && !brush->IsBox()) {
			for (const BrushPlane& plane : brush->Planes) {
				planes.push_back(plane.Normal.X);
				planes.push_back(plane.Normal.Y);
				planes.push_back(plane.Normal.Z);
				planes.push_back(plane.Distance);
			}
		}

		constexpr float32 cAxisEpsilon = 1e-5f;

		const Mat4f& m = local_to_world;
		const bool axis_aligned = planes.empty() && std::abs(m.Rows[0].Y) < cAxisEpsilon &&
								  std::abs(m.Rows[0].Z) < cAxisEpsilon && std::abs(m.Rows[1].X) < cAxisEpsilon &&
								  std::abs(m.Rows[1].Z) < cAxisEpsilon && std::abs(m.Rows[2].X) < cAxisEpsilon &&
								  std::abs(m.Rows[2].Y) < cAxisEpsilon;

		if (rx_placement_boxes_add(boxes.pBoxes, &object.Bounds.Min.mData[0], &object.Bounds.Max.mData[0],
								   &world_bounds.Min.mData[0], &world_bounds.Max.mData[0], &local_to_world.Rows[0].mData[0],
								   &world_to_local.Rows[0].mData[0], axis_aligned, planes.data(),
								   static_cast<uint32>(planes.size() / 4))) {
			num_boxes++;
		}

		rx_placement_boxes_extend(boxes.pBoxes, &world_bounds.Min.mData[0], &world_bounds.Max.mData[0]);
		num_objects++;
	}

	if (num_objects > num_boxes) {
		LogWarning("Probe placement only keeps probes out of the first {} of {} objects", num_boxes, num_objects);
	}

	boxes.UpdateBounds();

	return boxes;
}

uint32 FindNeededGridPoints(const Vec3f& volume_min, const Vec3f& cell_size, const ProbeGridSize& grid,
							const ProbePlacementBoxes& boxes, eProbeFill fill)
{
	const uint32 dims[3] = { grid.X, grid.Y, grid.Z };

	return rx_probe_count_needed(boxes.pBoxes, &volume_min.mData[0], &cell_size.mData[0], dims,
								 (fill == eProbeFill::Dense) ? 0 : 1);
}

ProbeGridSize GridForSpacing(const Vec3f& region_size, float32 spacing, bool cell_centred)
{
	uint32 dims[3];
	rx_probe_grid_for_spacing(&region_size.mData[0], spacing, cell_centred, dims);

	return ProbeGridSize { dims[0], dims[1], dims[2] };
}

void LayoutGrid(const Vec3f& region_min, const Vec3f& region_max, const ProbeGridSize& grid, bool cell_centred,
				Vec3f& out_min, Vec3f& out_size)
{
	const uint32 dims[3] = { grid.X, grid.Y, grid.Z };

	rx_probe_layout_grid(&region_min.mData[0], &region_max.mData[0], dims, cell_centred, &out_min.mData[0],
						 &out_size.mData[0]);
}

Vec3f GridCellSize(const Vec3f& volume_size, const ProbeGridSize& grid)
{
	const uint32 dims[3] = { grid.X, grid.Y, grid.Z };

	Vec3f cell_size;
	rx_probe_grid_cell_size(&volume_size.mData[0], dims, &cell_size.mData[0]);

	return cell_size;
}


bool CheckGridSize(const ProbeGridSize& grid)
{
	if (grid.IsValid()) {
		return true;
	}

	LogError("Probe volume rejected: a {}x{}x{} grid needs at least {} probes along each axis", grid.X, grid.Y, grid.Z,
			 Limits::MinProbeGridDim);

	return false;
}

void SetProbePosition(ProbeInfo& info, const Vec3f& position)
{
	info.ProbePosition[0] = position.X;
	info.ProbePosition[1] = position.Y;
	info.ProbePosition[2] = position.Z;
	info.ProbePosition[3] = 1.0f;
}

void ResetDepthMoments(ProbeInfo& info)
{
	for (uint32 texel = 0; texel < Limits::ProbeDepthFaces * Limits::ProbeDepthTexelsPerFace; texel++) {
		info.DepthMoments[texel * 2 + 0] = Limits::ProbeDepthMaxDistance;
		info.DepthMoments[texel * 2 + 1] = Limits::ProbeDepthMaxDistance * Limits::ProbeDepthMaxDistance;
	}
}

///////////////////////////////////
// GPU and file IO
///////////////////////////////////

void UploadRange(renderer::RawGpuBuffer& buffer, const void* data, uint64 offset, uint64 size)
{
	uint8* mapped = static_cast<uint8*>(buffer.GetMapped());
	if (mapped == nullptr) {
		return;
	}

	memcpy(mapped + offset, static_cast<const uint8*>(data) + offset, size);
	buffer.FlushToGpu(static_cast<uint32>(offset), static_cast<uint32>(size));
}


struct ProbeFileLayout
{
	char Magic[4] = { 'R', 'P', 'P', 'V' };
	uint32 Version = FX_PROBE_CACHE_FILE_VERSION;
	uint32 SHCoeffCount = Limits::ProbeSHCoeffCount;
	uint32 DepthFloatCount = Limits::ProbeDepthFloatCount;
	uint32 MaxVolumes = Limits::MaxProbeVolumes;
};

struct ProbeFileHeader
{
	ProbeFileLayout Layout;

	uint32 VolumeCount = 0;
	uint32 ProbeCount = 0;
	uint32 GridPointCount = 0;
};


void FinaliseVolumeBounds(ProbeVolumeData& volume)
{
	if (volume.DimsAndFirst[0] == 0 || volume.DimsAndFirst[1] == 0 || volume.DimsAndFirst[2] == 0) {
		volume.MaxAndCellVolume[0] = 0.0f;
		volume.MaxAndCellVolume[1] = 0.0f;
		volume.MaxAndCellVolume[2] = 0.0f;
		volume.MaxAndCellVolume[3] = FLT_MAX;
		return;
	}

	float32 cell_volume = 1.0f;

	for (uint32 axis = 0; axis < 3; axis++) {
		const float32 cell_size = 1.0f / std::max(volume.InvCellSize[axis], 1e-6f);
		const float32 cells = static_cast<float32>(volume.DimsAndFirst[axis] - 1);

		volume.MaxAndCellVolume[axis] = volume.MinAndCount[axis] + (cell_size * cells);
		cell_volume *= cell_size;
	}

	volume.MaxAndCellVolume[3] = cell_volume;
}

static constexpr uint32 scOldestReadableCacheVersion = FX_PROBE_CACHE_FILE_VERSION;

uint64 GetProbeFileSize(uint32 probe_count, uint32 grid_point_count)
{
	return sizeof(ProbeFileHeader) + Limits::MaxProbeVolumes * (sizeof(ProbeVolumeData) + sizeof(ProbeVolumeRange)) +
		   static_cast<uint64>(grid_point_count) * sizeof(uint16) +
		   static_cast<uint64>(probe_count) * (sizeof(ProbeSHData) + sizeof(ProbeInfo));
}

bool IsCacheLayoutReadable(const ProbeFileLayout& layout)
{
	const ProbeFileLayout expected {};

	return memcmp(layout.Magic, expected.Magic, sizeof(expected.Magic)) == 0 &&
		   layout.SHCoeffCount == expected.SHCoeffCount && layout.DepthFloatCount == expected.DepthFloatCount &&
		   layout.MaxVolumes == expected.MaxVolumes && layout.Version >= scOldestReadableCacheVersion &&
		   layout.Version <= FX_PROBE_CACHE_FILE_VERSION;
}

String GetProbeFilePath()
{
	if (gWorld != nullptr && gWorld->BlockoutPath.GetLength() > 0) {
		std::filesystem::path path(gWorld->BlockoutPath.CStr());
		path.replace_extension(".fxprobe");

		return String(path.string().c_str());
	}

	return String::Fmt("{}/probes.fxprobe", gAssetManager->GetScenePath().CStr());
}

/// Reads exactly `size` bytes from the current position in `file` into `out`
bool ReadExact(File& file, void* out, uint64 size)
{
	return file.Read(MakeSlice(static_cast<uint8*>(out), size)).Size == size;
}

} // namespace

void ProbeManager::Create()
{
	constexpr float32 cSky[3] = { 0.055f, 0.062f, 0.075f };
	constexpr float32 cGround[3] = { 0.025f, 0.022f, 0.020f };

	std::fill(std::begin(mProbes), std::end(mProbes), MakeSkyGradientProbe(cSky, cGround));


	for (ProbeInfo& info : mProbeInfos) {
		ResetDepthMoments(info);
	}

	ClearVolumes();

	AddVolumeAndPlaceProbes(Vec3f(-20.0f, -2.0f, -20.0f), Vec3f(150.0f, 15.0f, 150.0f), ProbeGridSize { 4, 2, 4 },
							ProbePlacementBoxes {}, eProbeFill::Dense);

	UploadReflectionProbes(0);
}

void ProbeManager::Destroy()
{
	mBakeState = eBakeState::Idle;
	mbCapturingFaces = false;
	mCurrentProbe = 0;

	if (!mbCaptureResourcesCreated) {
		return;
	}

	for (uint32 slot = 0; slot < scProbesPerFrame; slot++) {
		for (uint32 face = 0; face < scCaptureFaces; face++) {
			mColorStaging[slot][face].Destroy();
			mDepthStaging[slot][face].Destroy();
		}
	}

	for (renderer::RawGpuBuffer& staging : mReflectionStaging) {
		staging.Destroy();
	}

	rx_probe_capture_free(mpCapture);
	mpCapture = nullptr;

	mbCaptureResourcesCreated = false;
}

Vec3f ProbeManager::GetProbePosition(uint32 index) const
{
	const float32* position = mProbeInfos[index].ProbePosition;
	return Vec3f(position[0], position[1], position[2]);
}

///////////////////////////////////
// Placement
///////////////////////////////////

void ProbeManager::ClearVolumes()
{
	if (IsBaking()) {
		LogWarning("Cannot change the probe volumes while they are baking");
		return;
	}

	mVolumeCount = 0;
	mProbeCount = 0;
	mGridPointCount = 0;
	mbGridDirty = true;

	RefreshVolumeCounts();
	UploadToGpu(0, 0);
}

void ProbeManager::GetVolumeProbeRange(uint32 volume, uint32& out_first_probe, uint32& out_count) const
{
	Assert(volume < mVolumeCount);

	out_first_probe = mVolumeRanges[volume].FirstProbe;
	out_count = mVolumeRanges[volume].ProbeCount;
}

uint32 ProbeManager::GetVolumeOfProbe(uint32 probe_index) const
{
	for (uint32 volume = 0; volume < mVolumeCount; volume++) {
		const ProbeVolumeRange& range = mVolumeRanges[volume];

		if (probe_index >= range.FirstProbe && probe_index < range.FirstProbe + range.ProbeCount) {
			return volume;
		}
	}

	return Limits::MaxProbeVolumes;
}

bool ProbeManager::AddVolume(const Vec3f& center, const Vec3f& size, const ProbeGridSize& grid)
{
	if (IsBaking()) {
		LogWarning("Cannot add a probe volume while the probes are baking");
		return false;
	}

	return AddVolumeAndPlaceProbes(center - size * 0.5f, size, grid, GatherPlacementBoxes(), eProbeFill::Dense);
}

bool ProbeManager::AddLevelVolumes()
{
	if (IsBaking()) {
		LogWarning("Cannot add a probe volume while the probes are baking");
		return false;
	}

	const ProbePlacementBoxes boxes = GatherPlacementBoxes();

	if (boxes.IsEmpty()) {
		LogError("Cannot fit a probe volume to the level: there is no geometry to fit it to");
		return false;
	}

	const bool added_base = AddLevelBaseVolume(boxes);
	const bool added_surface = AddLevelSurfaceVolume(boxes);

	return added_base && added_surface;
}

bool ProbeManager::AddLevelBaseVolume(const ProbePlacementBoxes& boxes)
{
	const Vec3f level_size = boxes.Max - boxes.Min;

	float32 spacing = scBaseProbeSpacing;
	ProbeGridSize grid = GridForSpacing(level_size, spacing, true);

	while (grid.GetProbeCount() > scBaseProbeGridBudget && spacing <= scMaxProbeSpacing) {
		spacing *= 1.5f;
		grid = GridForSpacing(level_size, spacing, true);
	}

	Vec3f volume_min;
	Vec3f volume_size;
	LayoutGrid(boxes.Min, boxes.Max, grid, true, volume_min, volume_size);

	return AddVolumeAndPlaceProbes(volume_min, volume_size, grid, boxes, eProbeFill::Dense);
}

bool ProbeManager::AddLevelSurfaceVolume(const ProbePlacementBoxes& boxes)
{
	const float32 spacing = gCVars->Get("r_probe_level_spacing", scDefaultLevelProbeSpacing);
	return AddSurfaceVolumeForBudget(boxes.Min, boxes.Max, spacing, boxes, true);
}

bool ProbeManager::AddSurfaceVolumeForBudget(const Vec3f& region_min, const Vec3f& region_max, float32 spacing,
											 const ProbePlacementBoxes& boxes, bool cell_centred)
{
	if (mVolumeCount >= Limits::MaxProbeVolumes) {
		LogError("Probe volume rejected: all {} volume slots are taken", Limits::MaxProbeVolumes);
		return false;
	}

	const uint32 probe_budget = Limits::MaxIrradianceProbes - mProbeCount;
	const uint32 point_budget = Limits::MaxProbeGridPoints - mGridPointCount;

	for (float32 tried = std::max(spacing, 1e-2f); tried <= scMaxProbeSpacing; tried *= scSurfaceSpacingStep) {
		const ProbeGridSize grid = GridForSpacing(region_max - region_min, tried, cell_centred);

		if (grid.GetProbeCount() > point_budget) {
			continue;
		}

		Vec3f volume_min;
		Vec3f volume_size;
		LayoutGrid(region_min, region_max, grid, cell_centred, volume_min, volume_size);

		const uint32 num_needed = FindNeededGridPoints(volume_min, GridCellSize(volume_size, grid), grid, boxes,
													   eProbeFill::Surface);

		if (num_needed > probe_budget) {
			continue;
		}

		if (!AddVolumeAndPlaceProbes(volume_min, volume_size, grid, boxes, eProbeFill::Surface)) {
			continue;
		}

		if (tried > spacing) {
			LogWarning("Probe volume spacing raised from {}m to {}m to fit the {} probes left", spacing, tried,
					   probe_budget);
		}

		return true;
	}

	LogError("Probe volume rejected: no spacing up to {}m fits the {} probes left", scMaxProbeSpacing, probe_budget);
	return false;
}

uint32 ProbeManager::RebuildVolumesFromWorld()
{
	if (IsBaking()) {
		LogWarning("Cannot rebuild the probe volumes while they are baking");
		return 0;
	}

	ClearVolumes();

	const ProbePlacementBoxes boxes = GatherPlacementBoxes();

	if (boxes.IsEmpty()) {
		LogError("Cannot fit a probe volume to the level: there is no geometry to fit it to");
		return 0;
	}

	AddLevelBaseVolume(boxes);

	const float32 spacing = gCVars->Get("r_probe_spacing", scDefaultProbeSpacing);

	uint32 num_brush_volumes = 0;

	for (Object& object : gObjectManager->GetCache()) {
		if (!object.IsProbeVolume() || object.IsReflectionProbe()) {
			continue;
		}

		Vec3f min;
		Vec3f max;
		GetObjectWorldBounds(object, min, max);

		if (AddSurfaceVolumeForBudget(min, max, spacing, boxes, false)) {
			num_brush_volumes++;
		}
		else {
			LogWarning("Probe volume brush '{}' was skipped", object.Name.Get());
		}
	}

	AddLevelSurfaceVolume(boxes);

	RebuildReflectionProbesFromWorld();

	LogInfo("Probe volumes rebuilt: {} from editor brushes, {} in total, {} of {} probes and {} of {} grid points used",
			num_brush_volumes, mVolumeCount, mProbeCount, Limits::MaxIrradianceProbes, mGridPointCount,
			Limits::MaxProbeGridPoints);

	return num_brush_volumes;
}

void ProbeManager::BeginBake()
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	if (mVolumeCount == 0 || mProbeCount == 0) {
		LogError("Probe bake failed: no probes have been placed");
		return;
	}

	mCurrentProbe = 0;
	mCurrentBounce = 0;
	mBakePhase = eBakePhase::Irradiance;
	mBounceCount = static_cast<uint32>(
		std::clamp<int64>(gCVars->Get("r_probe_bounces", scDefaultProbeBounces), 1, scMaxProbeBounces));
	mBakeState = eBakeState::CapturePending;

	LogInfo("Probe bake started ({} probes across {} volume(s), {} per frame, {} bounce(s))", mProbeCount, mVolumeCount,
			scProbesPerFrame, mBounceCount);
}

void ProbeManager::BeginGridBake()
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	ClearVolumes();

	if (AddLevelVolumes()) {
		BeginBake();
	}
}

void ProbeManager::BeginGridBakeAt(const Vec3f& center, const Vec3f& size, const ProbeGridSize& grid)
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	if (!CheckGridSize(grid)) {
		return;
	}

	ClearVolumes();

	if (AddVolume(center, size, grid)) {
		BeginBake();
	}
}

void ProbeManager::RefreshVolumeCounts()
{
	// The shader reads the list's length out of whichever descriptor it has, so every entry carries it
	for (ProbeVolumeData& volume : mVolumes) {
		volume.MinAndCount[3] = static_cast<float32>(mVolumeCount);
	}
}

bool ProbeManager::AddVolumeAndPlaceProbes(const Vec3f& volume_min, const Vec3f& volume_size, const ProbeGridSize& grid,
										   const ProbePlacementBoxes& boxes, eProbeFill fill)
{
	if (!CheckGridSize(grid)) {
		return false;
	}

	if (mVolumeCount >= Limits::MaxProbeVolumes) {
		LogError("Probe volume rejected: all {} volume slots are taken", Limits::MaxProbeVolumes);
		return false;
	}

	const uint32 num_points = grid.GetProbeCount();

	if (num_points > Limits::MaxProbeGridPoints - mGridPointCount) {
		LogError("Probe volume rejected: its grid has {} points and only {} of the {} grid point budget are left",
				 num_points, Limits::MaxProbeGridPoints - mGridPointCount, Limits::MaxProbeGridPoints);
		return false;
	}

	const uint32 first_probe = mProbeCount;

	if (!PlaceVolumeProbes(mVolumeCount, volume_min, volume_size, grid, boxes, fill)) {
		return false;
	}

	mVolumeCount++;
	mbGridDirty = true;

	RefreshVolumeCounts();
	UploadToGpu(first_probe, mProbeCount - first_probe);

	return true;
}

bool ProbeManager::PlaceVolumeProbes(uint32 volume_index, const Vec3f& volume_min, const Vec3f& volume_size,
									 const ProbeGridSize& grid, const ProbePlacementBoxes& boxes, eProbeFill fill)
{
	const uint32 dims[3] = { grid.X, grid.Y, grid.Z };

	// Probes sit on the volume's boundary, so there are (dim - 1) cells along each axis
	const Vec3f num_cells(static_cast<float32>(dims[0] - 1), static_cast<float32>(dims[1] - 1),
						  static_cast<float32>(dims[2] - 1));

	const uint32 first_probe = mProbeCount;
	const uint32 first_point = mGridPointCount;
	const uint32 num_points = grid.GetProbeCount();

	RxPlacement* placement = rx_probe_place_volume(boxes.pBoxes, &volume_min.mData[0], &volume_size.mData[0], dims,
												   (fill == eProbeFill::Dense) ? 0 : 1,
												   Limits::MaxIrradianceProbes - first_probe);

	if (placement == nullptr) {
		LogWarning("Probe volume rejected: it needs more than the {} probes left of the {} probe budget",
				   Limits::MaxIrradianceProbes - first_probe, Limits::MaxIrradianceProbes);
		return false;
	}

	uint32 counts[6];
	rx_placement_counts(placement, counts);

	const uint32 num_needed = counts[0];
	const uint32 num_pushed = counts[1];
	const uint32 num_hugged = counts[2];
	const uint32 num_relocated = counts[3];
	const uint32 num_unplaced = counts[4];
	const uint32 num_placed = counts[5];

	std::vector<float32> positions(static_cast<size_t>(num_placed) * 3);
	rx_placement_positions(placement, positions.data());

	std::vector<uint32> placed_grid(num_points);
	rx_placement_grid(placement, placed_grid.data());

	rx_placement_free(placement);

	for (uint32 i = 0; i < num_placed; i++) {
		// The probes have moved, so whatever depth moments they hold were captured somewhere else. Reset them until
		// the bake fills them in, or unbaked probes would report occluders that aren't there.
		ResetDepthMoments(mProbeInfos[first_probe + i]);
		SetProbePosition(mProbeInfos[first_probe + i], Vec3f(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]));
	}

	for (uint32 point = 0; point < num_points; point++) {
		mGridProbes[first_point + point] = (placed_grid[point] == RX_PLACEMENT_NO_PROBE)
											   ? Limits::ProbeGridEmpty
											   : static_cast<uint16>(first_probe + placed_grid[point]);
	}

	const uint32 probe_index = first_probe + num_placed;
	const uint32 point_index = first_point + num_points;

	ProbeVolumeData& volume = mVolumes[volume_index];

	for (uint32 axis = 0; axis < 3; axis++) {
		volume.MinAndCount[axis] = volume_min.mData[axis];
		volume.InvCellSize[axis] = (volume_size.mData[axis] > 1e-4f) ? (num_cells.mData[axis] / volume_size.mData[axis])
																	 : 1.0f;
		volume.DimsAndFirst[axis] = dims[axis];
	}

	volume.InvCellSize[3] = 0.0f;
	volume.DimsAndFirst[3] = first_point;

	FinaliseVolumeBounds(volume);

	mVolumeRanges[volume_index] = ProbeVolumeRange { first_probe, probe_index - first_probe };

	mProbeCount = probe_index;
	mGridPointCount = point_index;

	LogInfo("Probe volume {} ({}): min={} size={} grid={}x{}x{}, {} of {} grid points needed, {} probes from index {} "
			"({} pushed out, {} hugging, {} relocated, {} unplaced)",
			volume_index, (fill == eProbeFill::Dense) ? "dense" : "surface", volume_min, volume_size, dims[0], dims[1],
			dims[2], num_needed, grid.GetProbeCount(), probe_index - first_probe, first_probe, num_pushed, num_hugged,
			num_relocated, num_unplaced);

	return true;
}

///////////////////////////////////
// Capture
///////////////////////////////////

void ProbeManager::CreateCaptureResources()
{
	if (mbCaptureResourcesCreated) {
		return;
	}

	mCaptureStage.Create("ProbeCapture", Vec2u(scCaptureSize, scCaptureSize), eSizeDivisor::FullRes);

	mCaptureStage.AddTarget(eImageFormat::RGBA16_Float,
							VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
								VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
							eImageAspectFlag::Color);

	mCaptureStage.AddTarget(eImageFormat::D32_Float,
							VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
								VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
							eImageAspectFlag::Depth);

	mCaptureStage.BuildRenderStage();

	constexpr uint64 cPixels = static_cast<uint64>(scCaptureSize) * scCaptureSize;

	for (uint32 slot = 0; slot < scProbesPerFrame; slot++) {
		for (uint32 face = 0; face < scCaptureFaces; face++) {
			mColorStaging[slot][face].Create(renderer::eGpuBufferType::Transfer, cPixels * sizeof(uint16) * 4,
											 RX_MEMORY_GPU_TO_CPU, eGpuBufferFlags::TransferReceiver);
			mDepthStaging[slot][face].Create(renderer::eGpuBufferType::Transfer, cPixels * sizeof(float32),
											 RX_MEMORY_GPU_TO_CPU, eGpuBufferFlags::TransferReceiver);
		}
	}

	constexpr uint32 cReflectionSize = Limits::ReflectionProbeSize;

	mReflectionStage.Create("ReflectionCapture", Vec2u(cReflectionSize, cReflectionSize), eSizeDivisor::FullRes);

	mReflectionStage.AddTarget(eImageFormat::RGBA16_Float,
							   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
								   VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
							   eImageAspectFlag::Color);

	mReflectionStage.AddTarget(eImageFormat::D32_Float,
							   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
							   eImageAspectFlag::Depth);

	mReflectionStage.BuildRenderStage();

	for (renderer::RawGpuBuffer& staging : mReflectionStaging) {
		staging.Create(renderer::eGpuBufferType::Transfer,
					   static_cast<uint64>(cReflectionSize) * cReflectionSize * sizeof(uint16) * 4,
					   RX_MEMORY_GPU_TO_CPU, eGpuBufferFlags::TransferReceiver);
	}

	BuildCaptureTexels();

	mbCaptureResourcesCreated = true;
}

void ProbeManager::BuildCaptureTexels()
{
	float32 face_inv_view_projection[scCaptureFaces][16];

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		const PerspectiveCamera camera = MakeCaptureCamera(Vec3f::sZero, face);
		const Mat4f inv_view_projection = camera.InvProjectionMatrix * camera.InvViewMatrix;

		mCaptureInvProjection = camera.InvProjectionMatrix;
		mCaptureFaceToClip[face] = inv_view_projection.Inverse();

		std::memcpy(face_inv_view_projection[face], inv_view_projection.RawData, sizeof(float32) * 16);
	}

	rx_probe_capture_free(mpCapture);
	mpCapture = rx_probe_capture_new(scCaptureSize, mCaptureInvProjection.RawData, &face_inv_view_projection[0][0]);

	Assert(mpCapture != nullptr);
}

void ProbeManager::RecordCaptureBatch(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face)
{
	if (mBakeState != eBakeState::CapturePending) {
		return;
	}

	CreateCaptureResources();

	if (mBakePhase == eBakePhase::Reflection) {
		RecordReflectionCapture(cmd, render_face);
		return;
	}

	mBatchStart = mCurrentProbe;
	const uint32 batch_end = std::min(mCurrentProbe + scProbesPerFrame, mProbeCount);

	mbCapturingFaces = true;

	for (; mCurrentProbe < batch_end; mCurrentProbe++) {
		const uint32 slot = mCurrentProbe - mBatchStart;

		for (uint32 face = 0; face < scCaptureFaces; face++) {
			PerspectiveCamera camera = MakeCaptureCamera(GetProbePosition(mCurrentProbe), face);

			render_face(camera, mCaptureStage);

			CopyTargetToStaging(cmd, mCaptureStage, scCaptureSize, eImageFormat::RGBA16_Float,
								mColorStaging[slot][face]);
			CopyTargetToStaging(cmd, mCaptureStage, scCaptureSize, eImageFormat::D32_Float, mDepthStaging[slot][face]);
		}
	}

	mbCapturingFaces = false;
	mBakeState = eBakeState::CaptureRecorded;
}

void ProbeManager::CopyTargetToStaging(renderer::CommandBuffer& cmd, renderer::RenderStage& stage, uint32 size,
									   eImageFormat format, renderer::RawGpuBuffer& staging)
{
	const renderer::TargetRef target = stage.GetTarget(format);
	Assert(target.IsValid());

	Image image = target.GetImage();

	const VkImageAspectFlags aspect = (format == eImageFormat::D32_Float) ? VK_IMAGE_ASPECT_DEPTH_BIT
																		  : VK_IMAGE_ASPECT_COLOR_BIT;

	renderer::BarrierHelper::ImageLayoutTransition(&image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmd, 0, 1);

	const VkBufferImageCopy copy {
		.imageSubresource { .aspectMask = aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
		.imageExtent { .width = size, .height = size, .depth = 1 },
	};

	vkCmdCopyImageToBuffer(cmd, image.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.Get(), 1, &copy);

	renderer::BarrierHelper::ImageLayoutTransition(&image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0, 1);
}

void ProbeManager::ServiceCaptureBake()
{
	if (mBakeState != eBakeState::CaptureRecorded) {
		return;
	}

	// The batch was recorded into this frame's command buffer, so wait for it to land in the staging buffers
	renderer::gGraphics->GetDevice()->WaitForIdle();

	if (mBakePhase == eBakePhase::Reflection) {
		ServiceReflectionBake();
		return;
	}

	for (uint32 probe = mBatchStart; probe < mCurrentProbe; probe++) {
		if (!ReadBackProbe(probe - mBatchStart, probe)) {
			LogError("Probe grid bake failed: could not read back probe {}", probe);
			mBakeState = eBakeState::Idle;
			return;
		}
	}

	UploadToGpu(mBatchStart, mCurrentProbe - mBatchStart);

	if (mCurrentProbe < mProbeCount) {
		mBakeState = eBakeState::CapturePending;
		LogInfo("Probe bake progress: {}/{}", mCurrentProbe, mProbeCount);
		return;
	}

	if (mCurrentBounce + 1 < mBounceCount) {
		mCurrentBounce++;
		mCurrentProbe = 0;
		mBakeState = eBakeState::CapturePending;
		LogInfo("Probe bounce {}/{} done, starting the next", mCurrentBounce, mBounceCount);
		return;
	}

	mBakeState = eBakeState::Idle;
	LogInfo("Probe bake complete ({} probes across {} volume(s), {} bounce(s))", mProbeCount, mVolumeCount,
			mBounceCount);

	if (mReflectionProbeCount > 0) {
		StartReflectionPhase();
	}
}

bool ProbeManager::ReadBackProbe(uint32 batch_slot, uint32 probe_index)
{
	const uint16* colors[scCaptureFaces];
	const float32* depths[scCaptureFaces];

	bool mapped = true;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		for (renderer::RawGpuBuffer* staging : { &mColorStaging[batch_slot][face], &mDepthStaging[batch_slot][face] }) {
			staging->Map();
			staging->InvalidateFromGpu();
		}

		colors[face] = static_cast<const uint16*>(mColorStaging[batch_slot][face].GetMapped());
		depths[face] = static_cast<const float32*>(mDepthStaging[batch_slot][face].GetMapped());

		mapped &= (colors[face] != nullptr && depths[face] != nullptr);
	}

	if (mapped) {
		ProjectCapture(colors, depths, mProbes[probe_index], mProbeInfos[probe_index]);
	}

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		mColorStaging[batch_slot][face].UnMap();
		mDepthStaging[batch_slot][face].UnMap();
	}

	return mapped;
}

void ProbeManager::ProjectCapture(const uint16* const colors[scCaptureFaces],
								  const float32* const depths[scCaptureFaces], ProbeSHData& out_sh,
								  ProbeInfo& out_info) const
{
	const int32 projected =
		rx_probe_capture_project(mpCapture, colors, depths, &out_sh.SH[0][0], out_info.DepthMoments);

	Assert(projected != 0);
}

void ProbeManager::UploadToGpu(uint32 first_probe, uint32 count)
{
	renderer::GraphicsBackend* graphics = renderer::gGraphics;

	// The probe buffers are shared by every frame in flight, so wait until none of them are reading
	graphics->GetDevice()->WaitForIdle();

	UploadRange(graphics->ProbeVolumeBuffer, mVolumes, 0, sizeof(mVolumes));

	if (mbGridDirty && mGridPointCount > 0) {
		const uint64 grid_bytes = static_cast<uint64>((mGridPointCount + 1) / 2) * sizeof(uint32);
		UploadRange(graphics->ProbeGridBuffer, mGridProbes, 0, grid_bytes);
	}

	mbGridDirty = false;

	if (count == 0) {
		return;
	}


	for (uint32 probe = first_probe; probe < first_probe + count; probe++) {
		// Position in the first three w lanes
		for (uint32 lane = 0; lane < 3; lane++) {
			mProbes[probe].SH[lane][3] = mProbeInfos[probe].ProbePosition[lane];
		}
	}

	UploadRange(graphics->ProbeBuffer, mProbes, first_probe * sizeof(ProbeSHData), count * sizeof(ProbeSHData));
	UploadMomentsAtlas(first_probe, count);
}

/// Encodes `value` (already in 0..1) the way VK_FORMAT_R16G16_UNORM decodes it.
static uint16 EncodeUNorm16(float32 value)
{
	return static_cast<uint16>(std::clamp(value, 0.0f, 1.0f) * 65535.0f + 0.5f);
}

void ProbeManager::UploadMomentsAtlas(uint32 first_probe, uint32 count)
{
	using namespace renderer;

	if (count == 0) {
		return;
	}

	GraphicsBackend* graphics = gGraphics;
	Assert(graphics->pProbeMomentsAtlas != nullptr);

	const uint32 first_row = first_probe / Limits::ProbeAtlasColumns;
	const uint32 last_row = (first_probe + count - 1) / Limits::ProbeAtlasColumns;

	constexpr uint32 cRowTexels = Limits::ProbeAtlasWidth * Limits::ProbeDepthSize;

	SizedArray<uint16> row;
	row.InitSize(static_cast<uint64>(cRowTexels) * 2);

	for (uint32 atlas_row = first_row; atlas_row <= last_row; atlas_row++) {
		// Probes past mProbeCount keep the max distance sentinel, which reads as unoccluded
		memset(row.pData, 0xFF, row.Size * sizeof(uint16));

		for (uint32 column = 0; column < Limits::ProbeAtlasColumns; column++) {
			const uint32 probe = (atlas_row * Limits::ProbeAtlasColumns) + column;

			if (probe >= mProbeCount) {
				break;
			}

			const float32* moments = mProbeInfos[probe].DepthMoments;
			const uint32 strip_x = column * Limits::ProbeAtlasProbeWidth;

			for (uint32 face = 0; face < Limits::ProbeDepthFaces; face++) {
				for (uint32 y = 0; y < Limits::ProbeDepthSize; y++) {
					for (uint32 x = 0; x < Limits::ProbeDepthSize; x++) {
						const uint32 src = ((face * Limits::ProbeDepthTexelsPerFace) + (y * Limits::ProbeDepthSize) +
											x) *
										   2;

						const uint32 dst = ((y * Limits::ProbeAtlasWidth) + strip_x + (face * Limits::ProbeDepthSize) +
											x) *
										   2;

						const float32 mean = moments[src + 0];

						// Stored as a standard deviation rather than the raw second moment: at 16 bits the shader's
						// mean_sq - mean * mean would lose the whole variance to cancellation at any distance.
						const float32 stddev = std::sqrt(std::max(moments[src + 1] - (mean * mean), 0.0f));

						row[dst + 0] = EncodeUNorm16(mean / Limits::ProbeDepthMaxDistance);
						row[dst + 1] = EncodeUNorm16(stddev / Limits::ProbeDepthMaxDistance);
					}
				}
			}
		}

		RawGpuBuffer staging;
		staging.Create(eGpuBufferType::Transfer, row.Size * sizeof(uint16), RX_MEMORY_CPU_TO_GPU,
					   eGpuBufferFlags::TransferReceiver);
		staging.Upload(row.pData, row.Size * sizeof(uint16));

		const Vec2u row_size(Limits::ProbeAtlasWidth, Limits::ProbeDepthSize);
		const Vec2u row_offset(0, atlas_row * Limits::ProbeDepthSize);

		graphics->SubmitImmediateUploadCmd(
			[&](CommandBuffer& cmd)
			{
				graphics->pProbeMomentsAtlas->CopyFromBuffer(cmd, staging, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
															 row_size, 0, 0, row_offset);
			});

		staging.Destroy();
	}
}

/////////////////////////////////////
// Probe cache (.fxprobe files)
/////////////////////////////////////

bool ProbeManager::SaveProbes()
{
	const bool saved = SaveIrradianceProbes();

	if (saved) {
		SaveReflectionProbes();
	}

	return saved;
}

bool ProbeManager::LoadProbes()
{
	const bool loaded = LoadIrradianceProbes();

	if (!loaded || !LoadReflectionProbes()) {
		mReflectionProbeCount = 0;
		mbReflectionsBaked = false;
		UploadReflectionProbes(0);
	}

	return loaded;
}

bool ProbeManager::SaveIrradianceProbes()
{
	if (IsBaking()) {
		LogWarning("Cannot save the probes while they are baking");
		return false;
	}

	const String path = GetProbeFilePath();

	File file(path, File::eModType::Write, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogError("Could not open {} to save the probes", path.CStr());
		return false;
	}

	ProbeFileHeader header;
	header.VolumeCount = mVolumeCount;
	header.ProbeCount = mProbeCount;
	header.GridPointCount = mGridPointCount;

	// Only the probes the volumes use are written, so a scene with a handful of small volumes gets a small file
	file.WriteRaw(&header, sizeof(header));
	file.WriteRaw(mVolumes, sizeof(mVolumes));
	file.WriteRaw(mVolumeRanges, sizeof(mVolumeRanges));
	file.WriteRaw(mGridProbes, mGridPointCount * sizeof(uint16));
	file.WriteRaw(mProbes, mProbeCount * sizeof(ProbeSHData));
	file.WriteRaw(mProbeInfos, mProbeCount * sizeof(ProbeInfo));

	LogInfo("Saved {} light probes across {} volume(s) to {}", mProbeCount, mVolumeCount, path.CStr());
	return true;
}

bool ProbeManager::LoadIrradianceProbes()
{
	const String path = GetProbeFilePath();

	File file(path, File::eModType::Read, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogInfo("No probe file at {}, using procedural probes", path.CStr());
		return false;
	}

	ProbeFileHeader header;

	if (!ReadExact(file, &header, sizeof(header)) || !IsCacheLayoutReadable(header.Layout)) {
		LogWarning("Probe file {} is out of date or not a probe file, rebake the probes", path.CStr());
		return false;
	}

	if (header.VolumeCount > Limits::MaxProbeVolumes || header.ProbeCount > Limits::MaxIrradianceProbes ||
		header.GridPointCount > Limits::MaxProbeGridPoints) {
		LogError("Probe file {} holds {} volumes, {} probes and {} grid points, past the limits of {}, {} and {}",
				 path.CStr(), header.VolumeCount, header.ProbeCount, header.GridPointCount, Limits::MaxProbeVolumes,
				 Limits::MaxIrradianceProbes, Limits::MaxProbeGridPoints);
		return false;
	}

	if (file.GetFileSize() != GetProbeFileSize(header.ProbeCount, header.GridPointCount)) {
		LogError("Probe file {} is {} bytes, not the {} its {} probes need", path.CStr(), file.GetFileSize(),
				 GetProbeFileSize(header.ProbeCount, header.GridPointCount), header.ProbeCount);
		return false;
	}

	ProbeVolumeData volumes[Limits::MaxProbeVolumes] {};
	ProbeVolumeRange ranges[Limits::MaxProbeVolumes] {};
	std::vector<uint16> grid(header.GridPointCount);

	if (!ReadExact(file, volumes, sizeof(volumes)) || !ReadExact(file, ranges, sizeof(ranges)) ||
		!ReadExact(file, grid.data(), grid.size() * sizeof(uint16))) {
		LogError("Could not read the probe volumes from {}", path.CStr());
		return false;
	}

	for (uint32 volume = 0; volume < header.VolumeCount; volume++) {
		const uint32* dims = volumes[volume].DimsAndFirst;
		const uint64 last_point = static_cast<uint64>(dims[3]) + static_cast<uint64>(dims[0]) * dims[1] * dims[2];
		const uint64 last_probe = static_cast<uint64>(ranges[volume].FirstProbe) + ranges[volume].ProbeCount;

		if (last_point > header.GridPointCount || last_probe > header.ProbeCount) {
			LogError("Probe file {} has a volume that runs past the probes it holds, rebake the probes", path.CStr());
			return false;
		}
	}

	for (uint16 probe : grid) {
		if (probe != Limits::ProbeGridEmpty && probe >= header.ProbeCount) {
			LogError("Probe file {} points at probe {} of {}, rebake the probes", path.CStr(), probe,
					 header.ProbeCount);
			return false;
		}
	}

	if (!ReadExact(file, mProbes, header.ProbeCount * sizeof(ProbeSHData)) ||
		!ReadExact(file, mProbeInfos, header.ProbeCount * sizeof(ProbeInfo))) {
		LogError("Could not read the probes from {}", path.CStr());
		return false;
	}

	memcpy(mVolumes, volumes, sizeof(mVolumes));
	memcpy(mVolumeRanges, ranges, sizeof(mVolumeRanges));
	std::copy(grid.begin(), grid.end(), mGridProbes);
	mVolumeCount = header.VolumeCount;
	mProbeCount = header.ProbeCount;
	mGridPointCount = header.GridPointCount;
	mbGridDirty = true;

	RefreshVolumeCounts();

	UploadToGpu(0, mProbeCount);

	LogInfo("Loaded {} light probes across {} volume(s) from {}", mProbeCount, mVolumeCount, path.CStr());
	return true;
}

namespace {

constexpr float32 scReflectionBlendDistance = 0.25f;
constexpr float32 scReflectionMinHalfExtent = 0.05f;
constexpr float32 scReflectionRelocation = 0.9f;
constexpr uint32 scReflectionFileVersion = 1;

constexpr uint64 scReflectionTexelsPerProbe = 6ull * (128 * 128 + 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8 + 4 * 4);
constexpr uint64 scReflectionHalfsPerProbe = scReflectionTexelsPerProbe * 4;

static_assert(Limits::ReflectionProbeSize == 128 && Limits::ReflectionProbeMips == 6);

struct ReflectionBox
{
	Mat4f BoxToWorld;
	Vec3f HalfExtent;
	float32 Volume;
};

struct ReflectionFileHeader
{
	char Magic[4] = { 'R', 'P', 'R', 'F' };
	uint32 Version = scReflectionFileVersion;
	uint32 Size = Limits::ReflectionProbeSize;
	uint32 Mips = Limits::ReflectionProbeMips;
	uint32 ProbeCount = 0;
};

ReflectionBox MakeReflectionBox(const Vec3f& local_min, const Vec3f& local_max, const Mat4f& local_to_world)
{
	const Vec3f half_extent = Vec3f::Max((local_max - local_min) * 0.5f, Vec3f(scReflectionMinHalfExtent));
	const Vec3f center = (local_max + local_min) * 0.5f;

	ReflectionBox box;
	box.BoxToWorld = Mat4f::AsScale(half_extent) * Mat4f::AsTranslation(center) * local_to_world;
	box.HalfExtent = half_extent;
	box.Volume = half_extent.X * half_extent.Y * half_extent.Z;

	return box;
}

String GetReflectionFilePath()
{
	std::filesystem::path path(GetProbeFilePath().CStr());
	path.replace_extension(".fxrefl");

	return String(path.string().c_str());
}

void SampleCaptureFace(const std::vector<float32>& face, uint32 size, float32 ndc_x, float32 ndc_y, float32 out[3])
{
	const float32 max_coord = static_cast<float32>(size - 1);

	const float32 x = std::clamp((ndc_x * 0.5f + 0.5f) * static_cast<float32>(size) - 0.5f, 0.0f, max_coord);
	const float32 y = std::clamp((ndc_y * 0.5f + 0.5f) * static_cast<float32>(size) - 0.5f, 0.0f, max_coord);

	const uint32 x0 = static_cast<uint32>(x);
	const uint32 y0 = static_cast<uint32>(y);
	const uint32 x1 = std::min(x0 + 1, size - 1);
	const uint32 y1 = std::min(y0 + 1, size - 1);

	const float32 fx = x - static_cast<float32>(x0);
	const float32 fy = y - static_cast<float32>(y0);

	const float32* t00 = face.data() + (y0 * size + x0) * 3;
	const float32* t10 = face.data() + (y0 * size + x1) * 3;
	const float32* t01 = face.data() + (y1 * size + x0) * 3;
	const float32* t11 = face.data() + (y1 * size + x1) * 3;

	for (uint32 c = 0; c < 3; c++) {
		const float32 top = t00[c] + (t10[c] - t00[c]) * fx;
		const float32 bottom = t01[c] + (t11[c] - t01[c]) * fx;

		out[c] = top + (bottom - top) * fy;
	}
}

} // namespace

Vec3f ProbeManager::GetReflectionProbePosition(uint32 index) const
{
	const float32* position = mReflectionProbes[index].PositionAndCount;
	return Vec3f(position[0], position[1], position[2]);
}

uint32 ProbeManager::RebuildReflectionProbesFromWorld()
{
	if (IsBaking()) {
		LogWarning("Cannot rebuild the reflection probes while the probes are baking");
		return 0;
	}

	const ProbePlacementBoxes boxes = GatherPlacementBoxes();

	std::vector<ReflectionBox> reflection_boxes;

	for (Object& object : gObjectManager->GetCache()) {
		if (object.IsReflectionProbe()) {
			reflection_boxes.push_back(
				MakeReflectionBox(object.Bounds.Min, object.Bounds.Max, object.GetWorldMatrix()));
		}
	}

	std::sort(reflection_boxes.begin(), reflection_boxes.end(),
			  [](const ReflectionBox& a, const ReflectionBox& b) { return a.Volume < b.Volume; });


	const uint32 num_brush_probes = static_cast<uint32>(reflection_boxes.size());
	const bool has_level_probe = !boxes.IsEmpty() && gCVars->Get("r_reflection_level_probe", int64(0)) != 0;

	uint32 max_brush_probes = Limits::MaxReflectionProbes;

	if (has_level_probe) {
		--max_brush_probes;
	}

	if (reflection_boxes.size() > max_brush_probes) {
		LogWarning("{} reflection probe brushes are placed but only {} fit, the largest are left out",
				   reflection_boxes.size(), max_brush_probes);
		reflection_boxes.resize(max_brush_probes);
	}

	if (has_level_probe) {
		reflection_boxes.push_back(MakeReflectionBox(boxes.Min, boxes.Max, Mat4f::scIdentity));
	}

	mReflectionProbeCount = static_cast<uint32>(reflection_boxes.size());
	mbReflectionsBaked = false;

	for (uint32 i = 0; i < mReflectionProbeCount; i++) {
		const ReflectionBox& box = reflection_boxes[i];
		ReflectionProbeData& probe = mReflectionProbes[i];

		const Vec4f center_h = box.BoxToWorld * Vec4f(0.0f, 0.0f, 0.0f, 1.0f);
		const Vec3f center(center_h.X, center_h.Y, center_h.Z);

		Vec3f position = center;

		const Vec3f max_relocation = box.HalfExtent * scReflectionRelocation;

		if (!rx_probe_position_valid(boxes.pBoxes, &center.mData[0], &center.mData[0]) &&
			!rx_probe_find_valid_position(boxes.pBoxes, &center.mData[0], &center.mData[0], &max_relocation.mData[0],
										  &position.mData[0])) {
			LogWarning("Reflection probe {} is captured from inside of the level's geometry, move its brush", i);
			position = center;
		}

		memcpy(probe.WorldToBox, box.BoxToWorld.Inverse().RawData, sizeof(probe.WorldToBox));

		probe.PositionAndCount[0] = position.X;
		probe.PositionAndCount[1] = position.Y;
		probe.PositionAndCount[2] = position.Z;
		probe.PositionAndCount[3] = 0.0f;

		probe.Fade[0] = box.HalfExtent.X / scReflectionBlendDistance;
		probe.Fade[1] = box.HalfExtent.Y / scReflectionBlendDistance;
		probe.Fade[2] = box.HalfExtent.Z / scReflectionBlendDistance;
		probe.Fade[3] = 0.0f;
	}

	UploadReflectionProbes(0);

	LogInfo("Reflection probes rebuilt: {} from editor brushes, {} in total",
			std::min(num_brush_probes, max_brush_probes), mReflectionProbeCount);

	return mReflectionProbeCount;
}

void ProbeManager::BeginReflectionBake()
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	if (mReflectionProbeCount == 0) {
		LogError("Reflection bake failed: no reflection probes have been placed");
		return;
	}

	StartReflectionPhase();
}

void ProbeManager::StartReflectionPhase()
{
	mBakePhase = eBakePhase::Reflection;
	mCurrentProbe = 0;
	mbReflectionsBaked = false;
	mBakeState = eBakeState::CapturePending;

	mReflectionTexels.assign(mReflectionProbeCount * scReflectionHalfsPerProbe, 0);

	UploadReflectionProbes(0);

	LogInfo("Reflection probe bake started ({} probes)", mReflectionProbeCount);
}

void ProbeManager::RecordReflectionCapture(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face)
{
	mBatchStart = mCurrentProbe;

	const Vec3f position = GetReflectionProbePosition(mCurrentProbe);

	mbCapturingFaces = true;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		PerspectiveCamera camera = MakeCaptureCamera(position, face);

		render_face(camera, mReflectionStage);

		CopyTargetToStaging(cmd, mReflectionStage, Limits::ReflectionProbeSize, eImageFormat::RGBA16_Float,
							mReflectionStaging[face]);
	}

	mbCapturingFaces = false;
	mCurrentProbe++;
	mBakeState = eBakeState::CaptureRecorded;
}

void ProbeManager::ServiceReflectionBake()
{
	const uint32 probe = mBatchStart;

	if (!ReadBackReflectionProbe(probe)) {
		LogError("Reflection probe bake failed: could not read back probe {}", probe);
		mBakePhase = eBakePhase::Irradiance;
		mBakeState = eBakeState::Idle;
		return;
	}

	UploadReflectionCubemap(probe);

	if (mCurrentProbe < mReflectionProbeCount) {
		mBakeState = eBakeState::CapturePending;
		LogInfo("Reflection probe bake progress: {}/{}", mCurrentProbe, mReflectionProbeCount);
		return;
	}

	mBakePhase = eBakePhase::Irradiance;
	mBakeState = eBakeState::Idle;
	mbReflectionsBaked = true;

	UploadReflectionProbes(mReflectionProbeCount);

	LogInfo("Reflection probe bake complete ({} probes)", mReflectionProbeCount);
}

bool ProbeManager::ReadBackReflectionProbe(uint32 probe_index)
{
	constexpr uint32 cSize = Limits::ReflectionProbeSize;
	constexpr uint32 cPixels = cSize * cSize;

	std::vector<float32> captures[scCaptureFaces];
	bool mapped = true;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		renderer::RawGpuBuffer& staging = mReflectionStaging[face];

		staging.Map();
		staging.InvalidateFromGpu();

		const uint16* rgba = static_cast<const uint16*>(staging.GetMapped());

		if (rgba == nullptr) {
			mapped = false;
		}
		else {
			captures[face].resize(cPixels * 3);

			for (uint32 pixel = 0; pixel < cPixels; pixel++) {
				for (uint32 c = 0; c < 3; c++) {
					captures[face][pixel * 3 + c] = std::clamp(HalfToFloat(rgba[pixel * 4 + c]), 0.0f, scRadianceClamp);
				}
			}
		}

		staging.UnMap();
	}

	if (!mapped) {
		return false;
	}

	std::vector<float32> cube[scCaptureFaces];
	const float32* cube_faces[scCaptureFaces];

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		cube[face].resize(cPixels * 3);
		float32* out = cube[face].data();

		for (uint32 y = 0; y < cSize; y++) {
			for (uint32 x = 0; x < cSize; x++, out += 3) {
				const float32 u = ((static_cast<float32>(x) + 0.5f) / static_cast<float32>(cSize)) * 2.0f - 1.0f;
				const float32 v = ((static_cast<float32>(y) + 0.5f) / static_cast<float32>(cSize)) * 2.0f - 1.0f;

				const Vec3f direction = ReflectionFilter::FaceUVToDirection(face, u, v);

				uint32 capture_face;
				float32 unused_u;
				float32 unused_v;
				ReflectionFilter::DirectionToFaceUV(direction, capture_face, unused_u, unused_v);

				const Vec4f clip = mCaptureFaceToClip[capture_face] *
								   Vec4f(direction.X, direction.Y, direction.Z, 1.0f);

				SampleCaptureFace(captures[capture_face], cSize, clip.X / clip.W, clip.Y / clip.W, out);
			}
		}

		cube_faces[face] = cube[face].data();
	}

	ReflectionFilter::Prefilter(cube_faces, cSize, Limits::ReflectionProbeMips,
								mReflectionTexels.data() + probe_index * scReflectionHalfsPerProbe);

	return true;
}

void ProbeManager::UploadReflectionProbes(uint32 visible_count)
{
	renderer::GraphicsBackend* graphics = renderer::gGraphics;

	graphics->GetDevice()->WaitForIdle();

	ReflectionProbeData gpu_probes[Limits::MaxReflectionProbes];
	memcpy(gpu_probes, mReflectionProbes, sizeof(gpu_probes));

	for (ReflectionProbeData& probe : gpu_probes) {
		probe.PositionAndCount[3] = static_cast<float32>(visible_count);
		probe.Fade[3] = static_cast<float32>(mReflectionProbeCount);
	}

	UploadRange(graphics->ReflectionProbeBuffer, gpu_probes, 0, sizeof(gpu_probes));
}

void ProbeManager::UploadReflectionCubemap(uint32 probe_index)
{
	using namespace renderer;

	GraphicsBackend* graphics = gGraphics;
	Image* image = graphics->pReflectionProbes;
	Assert(image != nullptr);

	graphics->GetDevice()->WaitForIdle();

	const uint64 bytes = scReflectionHalfsPerProbe * sizeof(uint16);

	RawGpuBuffer staging;
	staging.Create(eGpuBufferType::Transfer, bytes, RX_MEMORY_CPU_TO_GPU, eGpuBufferFlags::TransferReceiver);
	staging.Upload(mReflectionTexels.data() + probe_index * scReflectionHalfsPerProbe, bytes);

	VkBufferImageCopy regions[Limits::ReflectionProbeMips];
	uint64 offset = 0;

	for (uint32 mip = 0; mip < Limits::ReflectionProbeMips; mip++) {
		const uint32 mip_size = Limits::ReflectionProbeSize >> mip;

		regions[mip] = VkBufferImageCopy {
			.bufferOffset = offset,
			.imageSubresource {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = mip,
				.baseArrayLayer = probe_index * Limits::ReflectionProbeFaces,
				.layerCount = Limits::ReflectionProbeFaces,
			},
			.imageExtent { .width = mip_size, .height = mip_size, .depth = 1 },
		};

		offset += static_cast<uint64>(mip_size) * mip_size * Limits::ReflectionProbeFaces * 4 * sizeof(uint16);
	}

	graphics->SubmitImmediateUploadCmd(
		[&](CommandBuffer& cmd)
		{
			BarrierHelper::ImageLayoutTransition(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd, 0,
												 Limits::ReflectionProbeMips);

			vkCmdCopyBufferToImage(cmd, staging.Get(), image->Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
								   Limits::ReflectionProbeMips, regions);

			BarrierHelper::ImageLayoutTransition(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0,
												 Limits::ReflectionProbeMips);
		});

	staging.Destroy();
}

bool ProbeManager::SaveReflectionProbes()
{
	if (mReflectionProbeCount > 0 && !mbReflectionsBaked) {
		LogWarning("The reflection probes are not baked, so they were not saved");
		return false;
	}

	const String path = GetReflectionFilePath();

	if (mReflectionProbeCount == 0 && !File(path, File::eModType::Read, File::eDataType::Binary).IsFileOpen()) {
		return true;
	}

	File file(path, File::eModType::Write, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogError("Could not open {} to save the reflection probes", path.CStr());
		return false;
	}

	ReflectionFileHeader header;
	header.ProbeCount = mReflectionProbeCount;

	file.WriteRaw(&header, sizeof(header));
	file.WriteRaw(mReflectionProbes, mReflectionProbeCount * sizeof(ReflectionProbeData));
	file.WriteRaw(mReflectionTexels.data(), mReflectionProbeCount * scReflectionHalfsPerProbe * sizeof(uint16));

	LogInfo("Saved {} reflection probes to {}", mReflectionProbeCount, path.CStr());
	return true;
}

bool ProbeManager::LoadReflectionProbes()
{
	const String path = GetReflectionFilePath();

	File file(path, File::eModType::Read, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogInfo("No reflection probe file at {}", path.CStr());
		return false;
	}

	const ReflectionFileHeader expected {};
	ReflectionFileHeader header;

	if (!ReadExact(file, &header, sizeof(header)) || memcmp(header.Magic, expected.Magic, sizeof(header.Magic)) != 0 ||
		header.Version != expected.Version || header.Size != expected.Size || header.Mips != expected.Mips ||
		header.ProbeCount > Limits::MaxReflectionProbes) {
		LogWarning("Reflection probe file {} is out of date or not a reflection probe file, rebake the probes",
				   path.CStr());
		return false;
	}

	const uint64 texel_bytes = header.ProbeCount * scReflectionHalfsPerProbe * sizeof(uint16);
	const uint64 expected_size = sizeof(header) + header.ProbeCount * sizeof(ReflectionProbeData) + texel_bytes;

	if (file.GetFileSize() != expected_size) {
		LogError("Reflection probe file {} is {} bytes, not the {} its {} probes need", path.CStr(), file.GetFileSize(),
				 expected_size, header.ProbeCount);
		return false;
	}

	ReflectionProbeData probes[Limits::MaxReflectionProbes] {};
	std::vector<uint16> texels(header.ProbeCount * scReflectionHalfsPerProbe);

	if (!ReadExact(file, probes, header.ProbeCount * sizeof(ReflectionProbeData)) ||
		!ReadExact(file, texels.data(), texel_bytes)) {
		LogError("Could not read the reflection probes from {}", path.CStr());
		return false;
	}

	memcpy(mReflectionProbes, probes, sizeof(mReflectionProbes));
	mReflectionTexels = std::move(texels);
	mReflectionProbeCount = header.ProbeCount;
	mbReflectionsBaked = true;

	for (uint32 probe = 0; probe < mReflectionProbeCount; probe++) {
		UploadReflectionCubemap(probe);
	}

	UploadReflectionProbes(mReflectionProbeCount);

	LogInfo("Loaded {} reflection probes from {}", mReflectionProbeCount, path.CStr());
	return true;
}

} // namespace fx
