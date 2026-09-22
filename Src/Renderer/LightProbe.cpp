/*
 * File:        LightProbe.cpp
 * Author:      emd22
 * Created:     10/09/2026
 * Description: All light probe and light probe grid logic. Irradiance probes with baked L2 SH and depth moments for
 * visibility (VSM & DDGI-ish).
 */


#include "LightProbe.hpp"

#include <Asset/AssetManager.hpp>
#include <CVar.hpp>
#include <Core/File.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/BarrierHelper.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>

/// Bump whenever the layout or meaning of the cached data changes
#define FX_PROBE_CACHE_FILE_VERSION 9

namespace fx {

struct ProbePlacementBoxes
{
	struct Box
	{
		Vec3f Min;
		Vec3f Max;
	};

	static constexpr uint32 scMaxBoxes = 256;

	Box Boxes[scMaxBoxes];
	uint32 Count = 0;

	/// Bounds of all of the geometry, including any objects that didn't fit in `Boxes`
	Vec3f Min = Vec3f(std::numeric_limits<float32>::max());
	Vec3f Max = Vec3f(-std::numeric_limits<float32>::max());

	bool IsEmpty() const { return Min.X > Max.X; }
};

namespace {

///////////////////////////////////
// Spherical harmonics
///////////////////////////////////

constexpr float32 scSHNorm0 = 0.282095f;
constexpr float32 scSHNorm1 = 0.488603f;
constexpr float32 scSHNorm2 = 1.092548f;
constexpr float32 scSHNorm20 = 0.315392f;
constexpr float32 scSHNorm22 = 0.546274f;

/// Convolving radiance with the clamped cosine lobe scales each SH band l by A_l (Ramamoorthi & Hanrahan 2001). These
/// are A_l / pi, so evaluating the result gives irradiance / pi, which the shader multiplies by the diffuse albedo.
constexpr float32 scCosineLobe[Limits::ProbeSHCoeffCount] = {
	1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f,
};

/// Real SH basis up to L2. The order must match EvalProbeIrradiance() in Shaders/ProbeCommon.hlsli.
void EvalSHBasis(const Vec3f& d, float32 out[Limits::ProbeSHCoeffCount])
{
	out[0] = scSHNorm0;
	out[1] = scSHNorm1 * d.Y;
	out[2] = scSHNorm1 * d.Z;
	out[3] = scSHNorm1 * d.X;
	out[4] = scSHNorm2 * d.X * d.Y;
	out[5] = scSHNorm2 * d.Y * d.Z;
	out[6] = scSHNorm20 * (3.0f * d.Z * d.Z - 1.0f);
	out[7] = scSHNorm2 * d.X * d.Z;
	out[8] = scSHNorm22 * (d.X * d.X - d.Y * d.Y);
}

/// Irradiance / pi that is `ground` looking straight down, `sky` looking straight up, and blends linearly in between.
ProbeSHData MakeSkyGradientProbe(const float32 sky[3], const float32 ground[3])
{
	ProbeSHData probe {};

	for (uint32 c = 0; c < 3; c++) {
		probe.SH[0][c] = (sky[c] + ground[c]) * 0.5f / scSHNorm0;
		probe.SH[1][c] = (sky[c] - ground[c]) * 0.5f / scSHNorm1;
	}

	return probe;
}

///////////////////////////////////
// Capture
///////////////////////////////////

/// Brightest radiance a capture texel can contribute, so that a few very bright texels can't dominate a probe
constexpr float32 scRadianceClamp = 16.0f;

/// Camera for cube face `face` of a capture at `position`. This is the only place the face layout is defined, the bake
/// unprojects the captured texels with the same cameras.
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

	// Reverse-Z: the near plane argument is the far clip (see Mat4f::LoadPerspectiveMatrix), so this is a 0.1 to 200 m
	// frustum. It only has to reach past Limits::ProbeDepthMaxDistance, where baked distances saturate anyway.
	PerspectiveCamera camera(90.0f, 1.0f, 200.0f, 0.1f);

	camera.Position = position;
	camera.ViewMatrix.LookAt(position, position + scFaceDirections[face], scFaceUps[face]);
	camera.UpdateCameraMatrix();

	return camera;
}

/// NDC coordinate of the centre of capture texel `index` along one axis
float32 CaptureTexelToNdc(uint32 index)
{
	return ((static_cast<float32>(index) + 0.5f) / static_cast<float32>(ProbeManager::scCaptureSize)) * 2.0f - 1.0f;
}

/// Index of the depth moments texel that direction `d` falls in. Must match ProbeDepthDirectionToFaceUV() in
/// Shaders/ProbeCommon.hlsli.
uint32 DirectionToMomentTexel(const Vec3f& d)
{
	const Vec3f a = d.Abs();

	uint32 face;
	float32 u;
	float32 v;

	if (a.X >= a.Y && a.X >= a.Z) {
		face = (d.X > 0.0f) ? 0 : 1;
		u = ((d.X > 0.0f) ? -d.Z : d.Z) / a.X;
		v = -d.Y / a.X;
	}
	else if (a.Y >= a.Z) {
		face = (d.Y > 0.0f) ? 2 : 3;
		u = d.X / a.Y;
		v = ((d.Y > 0.0f) ? d.Z : -d.Z) / a.Y;
	}
	else {
		face = (d.Z > 0.0f) ? 4 : 5;
		u = ((d.Z > 0.0f) ? d.X : -d.X) / a.Z;
		v = -d.Y / a.Z;
	}

	constexpr uint32 cSize = Limits::ProbeDepthSize;

	const auto to_texel = [](float32 coord)
	{ return std::min(static_cast<uint32>(std::max((coord * 0.5f + 0.5f) * cSize, 0.0f)), cSize - 1); };

	return face * Limits::ProbeDepthTexelsPerFace + to_texel(v) * cSize + to_texel(u);
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

/// Distance from a capture's origin to what the capture texel at `ndc_x`, `ndc_y` hit. `stored_depth` is reverse-Z (the
/// viewport maps NDC depth z to 1 - z), and 0 where nothing was drawn.
float32 CaptureDepthToDistance(const Mat4f& inv_projection, float32 stored_depth, float32 ndc_x, float32 ndc_y)
{
	constexpr float32 cFar = Limits::ProbeDepthMaxDistance;

	if (stored_depth <= 0.0f) {
		return cFar;
	}

	// Mat4f * Vec4f treats the vector as a row, matching the engine's v * M convention
	const Vec4f view = inv_projection * Vec4f(ndc_x, ndc_y, 1.0f - stored_depth, 1.0f);
	const float32 distance = Vec3f(view.X, view.Y, view.Z).Length() / fabsf(view.W);

	// Also catches the NaN or infinity from a degenerate depth
	return (distance < cFar) ? distance : cFar;
}

///////////////////////////////////
// Placement
///////////////////////////////////

/// How far probes sit off the surfaces they are pushed out of or pulled onto
constexpr float32 scProbeSurfaceOffset = 0.25f;
/// Probes this close to the outside of a box still count as inside it
constexpr float32 scProbeInsideSkin = 0.05f;
/// Probes in open space further than this from a surface are left where they are
constexpr float32 scProbeHugMaxDistance = 1.5f;
constexpr uint32 scMaxPushOutIterations = 8;

/// Objects bigger than this (the sky) are left out of placement so that they can't blow up the volume
constexpr float32 scMaxPlacementObjectSize = 100.0f;
/// Padding around the level's geometry when fitting the volume to it
constexpr float32 scVolumePadding = 0.5f;

constexpr float32 scDefaultProbeSpacing = 2.5f;
constexpr float32 scMaxProbeSpacing = 64.0f;

void GetObjectWorldBounds(Object& object, Vec3f& out_min, Vec3f& out_max)
{
	// OBB::FromLocalBounds() does the same per-corner transform this used to do inline
	const AABB world_bounds = object.GetWorldOBB().GetWorldAABB();

	out_min = world_bounds.Min;
	out_max = world_bounds.Max;
}

ProbeGridSize MakeGridForSize(const Vec3f& size, float32 spacing, uint32 budget)
{
	const auto axis_dim = [](float32 extent, float32 axis_spacing)
	{
		const float32 cells = std::max(extent, 0.0f) / std::max(axis_spacing, 1e-3f);
		return std::max(static_cast<uint32>(std::lround(cells)) + 1, Limits::MinProbeGridDim);
	};

	for (float32 tried = std::max(spacing, 1e-3f); tried <= scMaxProbeSpacing; tried *= 1.5f) {
		const ProbeGridSize grid { axis_dim(size.X, tried), axis_dim(size.Y, tried), axis_dim(size.Z, tried) };

		if (grid.GetProbeCount() <= budget) {
			return grid;
		}
	}

	return ProbeGridSize { Limits::MinProbeGridDim, Limits::MinProbeGridDim, Limits::MinProbeGridDim };
}

ProbePlacementBoxes GatherPlacementBoxes()
{
	ProbePlacementBoxes boxes;
	uint32 num_objects = 0;

	for (Object& object : gObjectManager->GetCache()) {
		// Only the level's lit geometry. Unlit objects (the sky) and anything not probe visible (the view model) don't
		// count.
		if (!object.pMesh.IsValid() || object.IsUnlit() || !object.IsProbeVisible()) {
			continue;
		}

		Vec3f min;
		Vec3f max;
		GetObjectWorldBounds(object, min, max);

		if ((max - min).Length() > scMaxPlacementObjectSize) {
			continue;
		}

		if (boxes.Count < ProbePlacementBoxes::scMaxBoxes) {
			boxes.Boxes[boxes.Count++] = { min, max };
		}

		boxes.Min = Vec3f::Min(boxes.Min, min);
		boxes.Max = Vec3f::Max(boxes.Max, max);
		num_objects++;
	}

	if (num_objects > boxes.Count) {
		LogWarning("Probe placement only keeps probes out of the first {} of {} objects", boxes.Count, num_objects);
	}

	return boxes;
}

/// If `point` is inside `box`, moves it out through the nearest face to just off the surface. Returns true if it moved.
bool PushOutOfBox(Vec3f& point, const ProbePlacementBoxes::Box& box)
{
	for (uint32 axis = 0; axis < 3; axis++) {
		if (point.mData[axis] <= box.Min.mData[axis] - scProbeInsideSkin ||
			point.mData[axis] >= box.Max.mData[axis] + scProbeInsideSkin) {
			return false;
		}
	}

	uint32 exit_axis = 0;
	bool exit_through_max = false;
	float32 exit_distance = std::numeric_limits<float32>::max();

	for (uint32 axis = 0; axis < 3; axis++) {
		const float32 to_min = point.mData[axis] - box.Min.mData[axis];
		const float32 to_max = box.Max.mData[axis] - point.mData[axis];

		if (to_min < exit_distance) {
			exit_distance = to_min;
			exit_axis = axis;
			exit_through_max = false;
		}

		if (to_max < exit_distance) {
			exit_distance = to_max;
			exit_axis = axis;
			exit_through_max = true;
		}
	}

	point.mData[exit_axis] = exit_through_max ? box.Max.mData[exit_axis] + scProbeSurfaceOffset
											  : box.Min.mData[exit_axis] - scProbeSurfaceOffset;

	return true;
}

/// Pushes `point` out of any boxes it is inside. Returns true if it moved.
bool PushOutOfBoxes(Vec3f& point, const ProbePlacementBoxes& boxes)
{
	bool moved = false;

	// Leaving one box can land inside another, so repeat a few times
	for (uint32 iteration = 0; iteration < scMaxPushOutIterations; iteration++) {
		bool pushed = false;

		for (uint32 i = 0; i < boxes.Count && !pushed; i++) {
			pushed = PushOutOfBox(point, boxes.Boxes[i]);
		}

		if (!pushed) {
			break;
		}

		moved = true;
	}

	return moved;
}

/// Moves a probe in open space that is within `max_distance` of a surface to just off that surface, so that it samples
/// the lighting next to it rather than floating near it. Returns true if it moved.
bool HugNearestSurface(Vec3f& point, const ProbePlacementBoxes& boxes, float32 max_distance)
{
	float32 best_distance = max_distance;
	Vec3f best_point = point;
	bool found = false;

	for (uint32 i = 0; i < boxes.Count; i++) {
		const Vec3f closest = Vec3f::Clamp(point, boxes.Boxes[i].Min, boxes.Boxes[i].Max);
		const Vec3f to_point = point - closest;
		const float32 distance = to_point.Length();

		// Points on or inside a box are left to the push out
		if (distance < 1e-6f || distance >= best_distance) {
			continue;
		}

		best_point = closest + to_point * (scProbeSurfaceOffset / distance);
		best_distance = distance;
		found = true;
	}

	point = best_point;
	return found;
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
	info.ProbePosition[3] = 0.0f;
}

/// Puts a probe's depth moments back to "nothing hit". A probe that has moved was captured somewhere else, and a
/// probe that has never been captured has zeroed moments, which would read as an occluder sitting on top of it.
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

/// Copies `size` bytes at `offset` in `data` to the same offset in `buffer`
void UploadRange(renderer::RawGpuBuffer& buffer, const void* data, uint64 offset, uint64 size)
{
	uint8* mapped = static_cast<uint8*>(buffer.pMappedBuffer);
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
	/// The volume array is written whole, so its length is part of the layout. The probe count isn't: only the
	/// probes the volumes actually use are written.
	uint32 MaxVolumes = Limits::MaxProbeVolumes;
};

struct ProbeFileHeader
{
	ProbeFileLayout Layout;

	uint32 VolumeCount = 0;
	uint32 ProbeCount = 0;
};


struct ProbeVolumeDataV8
{
	float32 MinAndCount[4];
	float32 InvCellSize[4];
	uint32 DimsAndFirst[4];
};

static_assert(sizeof(ProbeVolumeDataV8) == 48, "The version 8 volume layout is fixed, it describes files on disk");

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

static constexpr uint32 scOldestReadableCacheVersion = 8;

uint64 GetProbeFileSize(uint32 probe_count, uint32 version)
{
	const uint64 volume_stride = (version >= 9) ? sizeof(ProbeVolumeData) : sizeof(ProbeVolumeDataV8);

	return sizeof(ProbeFileHeader) + Limits::MaxProbeVolumes * volume_stride +
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

String GetProbeFilePath() { return String::Fmt("{}/probes.fxprobe", gAssetManager->GetScenePath().CStr()); }

/// Reads exactly `size` bytes from the current position in `file` into `out`
bool ReadExact(File& file, void* out, uint64 size)
{
	return file.Read(MakeSlice(static_cast<uint8*>(out), size)).Size == size;
}

} // namespace

void ProbeManager::Create()
{
	// Until probes are baked or loaded everything is lit by a dim sky gradient. Every probe is the same, so it doesn't

	constexpr float32 cSky[3] = { 0.055f, 0.062f, 0.075f };
	constexpr float32 cGround[3] = { 0.025f, 0.022f, 0.020f };

	std::fill(std::begin(mProbes), std::end(mProbes), MakeSkyGradientProbe(cSky, cGround));


	for (ProbeInfo& info : mProbeInfos) {
		ResetDepthMoments(info);
	}

	ClearVolumes();

	// The object cache is empty this early, so there is nothing to keep the probes out of
	AddVolumeAndPlaceProbes(Vec3f(-20.0f, -2.0f, -20.0f), Vec3f(150.0f, 15.0f, 150.0f), ProbeGridSize { 4, 2, 4 },
							ProbePlacementBoxes {});
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

	mCaptureTexels.Free();

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

	RefreshVolumeCounts();
	UploadToGpu(0, 0);
}

void ProbeManager::GetVolumeProbeRange(uint32 volume, uint32& out_first_probe, uint32& out_count) const
{
	Assert(volume < mVolumeCount);

	const uint32* dims = mVolumes[volume].DimsAndFirst;

	out_first_probe = dims[3];
	out_count = dims[0] * dims[1] * dims[2];
}

uint32 ProbeManager::GetVolumeOfProbe(uint32 probe_index) const
{
	for (uint32 volume = 0; volume < mVolumeCount; volume++) {
		uint32 first;
		uint32 count;
		GetVolumeProbeRange(volume, first, count);

		if (probe_index >= first && probe_index < first + count) {
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

	return AddVolumeAndPlaceProbes(center - size * 0.5f, size, grid, GatherPlacementBoxes());
}

bool ProbeManager::AddLevelVolume(const ProbeGridSize& grid)
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

	// Pad the volume so that the outermost probes sit off the level's outer surfaces, but never put it below the ground
	Vec3f volume_min = boxes.Min - Vec3f(scVolumePadding);
	volume_min.Y = std::max(volume_min.Y, 0.0f);

	const Vec3f volume_max = boxes.Max + Vec3f(scVolumePadding);

	return AddVolumeAndPlaceProbes(volume_min, volume_max - volume_min, grid, boxes);
}

uint32 ProbeManager::RebuildVolumesFromWorld()
{
	if (IsBaking()) {
		LogWarning("Cannot rebuild the probe volumes while they are baking");
		return 0;
	}

	ClearVolumes();

	AddLevelVolume();

	const float32 spacing = gCVars->Get("r_probe_spacing", scDefaultProbeSpacing);
	const ProbePlacementBoxes boxes = GatherPlacementBoxes();

	uint32 num_brush_volumes = 0;

	for (Object& object : gObjectManager->GetCache()) {
		if (!object.IsProbeVolume()) {
			continue;
		}

		Vec3f min;
		Vec3f max;
		GetObjectWorldBounds(object, min, max);

		const Vec3f size = max - min;
		const uint32 budget = Limits::MaxIrradianceProbes - mProbeCount;

		if (AddVolumeAndPlaceProbes(min, size, MakeGridForSize(size, spacing, budget), boxes)) {
			num_brush_volumes++;
		}
		else {
			LogWarning("Probe volume brush '{}' was skipped", object.Name.Get());
		}
	}

	LogInfo("Probe volumes rebuilt: {} from editor brushes, {} in total, {} of {} probes used", num_brush_volumes,
			mVolumeCount, mProbeCount, Limits::MaxIrradianceProbes);

	return num_brush_volumes;
}

void ProbeManager::BeginBake()
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	if (mVolumeCount == 0) {
		LogError("Probe bake failed: no probe volumes have been added");
		return;
	}

	mCurrentProbe = 0;
	mBakeState = eBakeState::CapturePending;

	LogInfo("Probe bake started ({} probes across {} volume(s), {} per frame)", mProbeCount, mVolumeCount,
			scProbesPerFrame);
}

void ProbeManager::BeginGridBake()
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	const ProbePlacementBoxes boxes = GatherPlacementBoxes();

	if (boxes.IsEmpty()) {
		LogError("Probe grid bake failed: there is no geometry to fit the probe volume to");
		return;
	}

	Vec3f volume_min = boxes.Min - Vec3f(scVolumePadding);
	volume_min.Y = std::max(volume_min.Y, 0.0f);

	const Vec3f volume_max = boxes.Max + Vec3f(scVolumePadding);

	StartSingleVolumeBake(volume_min, volume_max - volume_min, ProbeGridSize {}, boxes);
}

void ProbeManager::BeginGridBakeAt(const Vec3f& center, const Vec3f& size, const ProbeGridSize& grid)
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	StartSingleVolumeBake(center - size * 0.5f, size, grid, GatherPlacementBoxes());
}

void ProbeManager::StartSingleVolumeBake(const Vec3f& volume_min, const Vec3f& volume_size, const ProbeGridSize& grid,
										 const ProbePlacementBoxes& boxes)
{
	// Checked before the clear, so that a rejected grid leaves the volumes that were already there alone
	if (!CheckGridSize(grid)) {
		return;
	}

	ClearVolumes();

	if (!AddVolumeAndPlaceProbes(volume_min, volume_size, grid, boxes)) {
		return;
	}

	BeginBake();
}

void ProbeManager::RefreshVolumeCounts()
{
	// The shader reads the list's length out of whichever descriptor it has, so every entry carries it
	for (ProbeVolumeData& volume : mVolumes) {
		volume.MinAndCount[3] = static_cast<float32>(mVolumeCount);
	}
}

bool ProbeManager::AddVolumeAndPlaceProbes(const Vec3f& volume_min, const Vec3f& volume_size, const ProbeGridSize& grid,
										   const ProbePlacementBoxes& boxes)
{
	if (!CheckGridSize(grid)) {
		return false;
	}

	if (mVolumeCount >= Limits::MaxProbeVolumes) {
		LogError("Probe volume rejected: all {} volume slots are taken", Limits::MaxProbeVolumes);
		return false;
	}

	const uint32 num_probes = grid.GetProbeCount();
	const uint32 first_probe = mProbeCount;

	if (num_probes > Limits::MaxIrradianceProbes - first_probe) {
		LogError("Probe volume rejected: it needs {} probes and only {} of the {} probe budget are left", num_probes,
				 Limits::MaxIrradianceProbes - first_probe, Limits::MaxIrradianceProbes);
		return false;
	}

	const uint32 volume_index = mVolumeCount;

	PlaceVolumeProbes(volume_index, first_probe, volume_min, volume_size, grid, boxes);

	mVolumeCount++;
	mProbeCount += num_probes;

	RefreshVolumeCounts();
	UploadToGpu(first_probe, num_probes);

	return true;
}

void ProbeManager::PlaceVolumeProbes(uint32 volume_index, uint32 first_probe, const Vec3f& volume_min,
									 const Vec3f& volume_size, const ProbeGridSize& grid,
									 const ProbePlacementBoxes& boxes)
{
	const uint32 dims[3] = { grid.X, grid.Y, grid.Z };

	// Probes sit on the volume's boundary, so there are (dim - 1) cells along each axis
	const Vec3f num_cells(static_cast<float32>(dims[0] - 1), static_cast<float32>(dims[1] - 1),
						  static_cast<float32>(dims[2] - 1));

	const Vec3f cell_size = volume_size / num_cells;
	const Vec3f volume_max = volume_min + volume_size;

	const float32 hug_distance = std::min(
		{ scProbeHugMaxDistance, 0.5f * cell_size.X, 0.5f * cell_size.Y, 0.5f * cell_size.Z });

	uint32 num_pushed = 0;
	uint32 num_hugged = 0;
	uint32 probe_index = first_probe;

	for (uint32 iz = 0; iz < dims[2]; iz++) {
		for (uint32 iy = 0; iy < dims[1]; iy++) {
			for (uint32 ix = 0; ix < dims[0]; ix++) {
				Vec3f position = volume_min + cell_size * Vec3f(static_cast<float32>(ix), static_cast<float32>(iy),
																static_cast<float32>(iz));

				if (PushOutOfBoxes(position, boxes)) {
					num_pushed++;
				}

				if (HugNearestSurface(position, boxes, hug_distance)) {
					// The surface of one box can be inside another
					PushOutOfBoxes(position, boxes);

					// The shader's trilinear lookup only works for probes inside the volume
					position = Vec3f::Clamp(position, volume_min, volume_max);
					num_hugged++;
				}

				// The probes have moved, so whatever depth moments they hold were captured somewhere else. Reset
				// them until the bake fills them in, or unbaked probes would report occluders that aren't there.
				ResetDepthMoments(mProbeInfos[probe_index]);

				SetProbePosition(mProbeInfos[probe_index++], position);
			}
		}
	}

	ProbeVolumeData& volume = mVolumes[volume_index];

	for (uint32 axis = 0; axis < 3; axis++) {
		volume.MinAndCount[axis] = volume_min.mData[axis];
		volume.InvCellSize[axis] = (volume_size.mData[axis] > 1e-4f) ? (num_cells.mData[axis] / volume_size.mData[axis])
																	 : 1.0f;
		volume.DimsAndFirst[axis] = dims[axis];
	}

	volume.InvCellSize[3] = 0.0f;
	volume.DimsAndFirst[3] = first_probe;

	FinaliseVolumeBounds(volume);

	LogInfo("Probe volume {}: min={} size={} grid={}x{}x{} ({} probes from index {}, {} pushed out, {} hugging)",
			volume_index, volume_min, volume_size, dims[0], dims[1], dims[2], grid.GetProbeCount(), first_probe,
			num_pushed, num_hugged);
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
											 VMA_MEMORY_USAGE_GPU_TO_CPU, eGpuBufferFlags::TransferReceiver);
			mDepthStaging[slot][face].Create(renderer::eGpuBufferType::Transfer, cPixels * sizeof(float32),
											 VMA_MEMORY_USAGE_GPU_TO_CPU, eGpuBufferFlags::TransferReceiver);
		}
	}

	BuildCaptureTexels();

	mbCaptureResourcesCreated = true;
}

void ProbeManager::BuildCaptureTexels()
{
	// NDC area of one capture texel
	constexpr float32 cTexelArea = 4.0f / static_cast<float32>(scCaptureSize * scCaptureSize);

	mCaptureTexels.InitSize(scCaptureFaces * scCaptureSize * scCaptureSize);
	CaptureTexel* texel = mCaptureTexels.pData;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		// Every probe's faces point the same way, so a capture at the origin gives the directions for all of them
		const PerspectiveCamera camera = MakeCaptureCamera(Vec3f::sZero, face);
		const Mat4f inv_view_projection = camera.InvProjectionMatrix * camera.InvViewMatrix;

		mCaptureInvProjection = camera.InvProjectionMatrix;

		for (uint32 y = 0; y < scCaptureSize; y++) {
			for (uint32 x = 0; x < scCaptureSize; x++, texel++) {
				const float32 ndc_x = CaptureTexelToNdc(x);
				const float32 ndc_y = CaptureTexelToNdc(y);

				const Vec4f point = inv_view_projection * Vec4f(ndc_x, ndc_y, 0.5f, 1.0f);
				const Vec3f direction = Vec3f(point.X / point.W, point.Y / point.W, point.Z / point.W).Normalize();

				// Solid angle of a texel on a 90 degree cube face
				const float32 dist_sq = 1.0f + ndc_x * ndc_x + ndc_y * ndc_y;
				texel->SolidAngle = cTexelArea / (dist_sq * sqrtf(dist_sq));

				EvalSHBasis(direction, texel->WeightedBasis);

				for (float32& basis : texel->WeightedBasis) {
					basis *= texel->SolidAngle;
				}

				texel->MomentTexel = DirectionToMomentTexel(direction);
			}
		}
	}
}

void ProbeManager::RecordCaptureBatch(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face)
{
	if (mBakeState != eBakeState::CapturePending) {
		return;
	}

	CreateCaptureResources();

	mBatchStart = mCurrentProbe;
	const uint32 batch_end = std::min(mCurrentProbe + scProbesPerFrame, mProbeCount);

	mbCapturingFaces = true;

	for (; mCurrentProbe < batch_end; mCurrentProbe++) {
		const uint32 slot = mCurrentProbe - mBatchStart;

		for (uint32 face = 0; face < scCaptureFaces; face++) {
			PerspectiveCamera camera = MakeCaptureCamera(GetProbePosition(mCurrentProbe), face);

			render_face(camera, mCaptureStage);

			CopyTargetToStaging(cmd, eImageFormat::RGBA16_Float, mColorStaging[slot][face]);
			CopyTargetToStaging(cmd, eImageFormat::D32_Float, mDepthStaging[slot][face]);
		}
	}

	mbCapturingFaces = false;
	mBakeState = eBakeState::CaptureRecorded;
}

void ProbeManager::CopyTargetToStaging(renderer::CommandBuffer& cmd, eImageFormat format,
									   renderer::RawGpuBuffer& staging)
{
	renderer::Target* target = mCaptureStage.GetTarget(format);
	Assert(target != nullptr);

	Image& image = target->Image;

	const VkImageAspectFlags aspect = (format == eImageFormat::D32_Float) ? VK_IMAGE_ASPECT_DEPTH_BIT
																		  : VK_IMAGE_ASPECT_COLOR_BIT;

	renderer::BarrierHelper::ImageLayoutTransition(&image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmd, 0, 1);

	const VkBufferImageCopy copy {
		.imageSubresource { .aspectMask = aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
		.imageExtent { .width = scCaptureSize, .height = scCaptureSize, .depth = 1 },
	};

	vkCmdCopyImageToBuffer(cmd, image.InternalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.Buffer, 1, &copy);

	renderer::BarrierHelper::ImageLayoutTransition(&image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0, 1);
}

void ProbeManager::ServiceCaptureBake()
{
	if (mBakeState != eBakeState::CaptureRecorded) {
		return;
	}

	// The batch was recorded into this frame's command buffer, so wait for it to land in the staging buffers
	renderer::gGraphics->GetDevice()->WaitForIdle();

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

	mBakeState = eBakeState::Idle;
	LogInfo("Probe bake complete ({} probes across {} volume(s))", mProbeCount, mVolumeCount);
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

		colors[face] = static_cast<const uint16*>(mColorStaging[batch_slot][face].pMappedBuffer);
		depths[face] = static_cast<const float32*>(mDepthStaging[batch_slot][face].pMappedBuffer);

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
	constexpr uint32 cNumMomentTexels = Limits::ProbeDepthFaces * Limits::ProbeDepthTexelsPerFace;
	constexpr float32 cFar = Limits::ProbeDepthMaxDistance;

	float32 sh[Limits::ProbeSHCoeffCount][3] = {};

	// Solid angle weighted sums of distance and distance squared for each moments texel
	float32 moment_weight[cNumMomentTexels] = {};
	float32 moment_sum[cNumMomentTexels] = {};
	float32 moment_sum_sq[cNumMomentTexels] = {};

	const CaptureTexel* texel = mCaptureTexels.pData;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		for (uint32 y = 0; y < scCaptureSize; y++) {
			for (uint32 x = 0; x < scCaptureSize; x++, texel++) {
				const uint32 pixel = y * scCaptureSize + x;
				const uint16* rgba = colors[face] + pixel * 4;

				for (uint32 c = 0; c < 3; c++) {
					const float32 radiance = std::clamp(HalfToFloat(rgba[c]), 0.0f, scRadianceClamp);

					for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
						sh[k][c] += radiance * texel->WeightedBasis[k];
					}
				}

				const float32 distance = CaptureDepthToDistance(mCaptureInvProjection, depths[face][pixel],
																CaptureTexelToNdc(x), CaptureTexelToNdc(y));

				moment_weight[texel->MomentTexel] += texel->SolidAngle;
				moment_sum[texel->MomentTexel] += distance * texel->SolidAngle;
				moment_sum_sq[texel->MomentTexel] += distance * distance * texel->SolidAngle;
			}
		}
	}

	for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
		for (uint32 c = 0; c < 3; c++) {
			out_sh.SH[k][c] = sh[k][c] * scCosineLobe[k];
		}

		out_sh.SH[k][3] = 0.0f;
	}

	for (uint32 t = 0; t < cNumMomentTexels; t++) {
		float32 mean = cFar;
		float32 mean_sq = cFar * cFar;

		if (moment_weight[t] > 1e-9f) {
			mean = moment_sum[t] / moment_weight[t];
			// Variance can't be negative, but float error can make it so
			mean_sq = std::max(moment_sum_sq[t] / moment_weight[t], mean * mean);
		}

		out_info.DepthMoments[t * 2 + 0] = mean;
		out_info.DepthMoments[t * 2 + 1] = mean_sq;
	}
}

void ProbeManager::UploadToGpu(uint32 first_probe, uint32 count)
{
	renderer::GraphicsBackend* graphics = renderer::gGraphics;

	// The probe buffers are shared by every frame in flight, so wait until none of them are reading
	graphics->GetDevice()->WaitForIdle();

	UploadRange(graphics->ProbeVolumeBuffer, mVolumes, 0, sizeof(mVolumes));

	if (count == 0) {
		return;
	}


	for (uint32 probe = first_probe; probe < first_probe + count; probe++) {
		for (uint32 axis = 0; axis < 3; axis++) {
			mProbes[probe].SH[axis][3] = mProbeInfos[probe].ProbePosition[axis];
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
		staging.Create(eGpuBufferType::Transfer, row.Size * sizeof(uint16), VMA_MEMORY_USAGE_CPU_TO_GPU,
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

	// Only the probes the volumes use are written, so a scene with a handful of small volumes gets a small file
	file.WriteRaw(&header, sizeof(header));
	file.WriteRaw(mVolumes, sizeof(mVolumes));
	file.WriteRaw(mProbes, mProbeCount * sizeof(ProbeSHData));
	file.WriteRaw(mProbeInfos, mProbeCount * sizeof(ProbeInfo));

	LogInfo("Saved {} light probes across {} volume(s) to {}", mProbeCount, mVolumeCount, path.CStr());
	return true;
}

bool ProbeManager::LoadProbes()
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

	if (header.VolumeCount > Limits::MaxProbeVolumes || header.ProbeCount > Limits::MaxIrradianceProbes) {
		LogError("Probe file {} holds {} volumes and {} probes, past the limits of {} and {}", path.CStr(),
				 header.VolumeCount, header.ProbeCount, Limits::MaxProbeVolumes, Limits::MaxIrradianceProbes);
		return false;
	}

	if (file.GetFileSize() != GetProbeFileSize(header.ProbeCount, header.Layout.Version)) {
		LogError("Probe file {} is {} bytes, not the {} its {} probes need", path.CStr(), file.GetFileSize(),
				 GetProbeFileSize(header.ProbeCount, header.Layout.Version), header.ProbeCount);
		return false;
	}

	ProbeVolumeData volumes[Limits::MaxProbeVolumes] {};

	if (header.Layout.Version >= 9) {
		if (!ReadExact(file, volumes, sizeof(volumes))) {
			LogError("Could not read the probe volumes from {}", path.CStr());
			return false;
		}
	}
	else {
		// Version 8 predates MaxAndCellVolume, which is derived, so the bake itself is still good
		ProbeVolumeDataV8 legacy[Limits::MaxProbeVolumes];

		if (!ReadExact(file, legacy, sizeof(legacy))) {
			LogError("Could not read the probe volumes from {}", path.CStr());
			return false;
		}

		for (uint32 volume = 0; volume < Limits::MaxProbeVolumes; volume++) {
			memcpy(volumes[volume].MinAndCount, legacy[volume].MinAndCount, sizeof(legacy[volume].MinAndCount));
			memcpy(volumes[volume].InvCellSize, legacy[volume].InvCellSize, sizeof(legacy[volume].InvCellSize));
			memcpy(volumes[volume].DimsAndFirst, legacy[volume].DimsAndFirst, sizeof(legacy[volume].DimsAndFirst));

			FinaliseVolumeBounds(volumes[volume]);
		}

		LogInfo("Migrated probe cache {} from version {} to {}", path.CStr(), header.Layout.Version,
				FX_PROBE_CACHE_FILE_VERSION);
	}

	// A volume whose probes run past what the file holds would send the shader into probes that were never placed
	for (uint32 volume = 0; volume < header.VolumeCount; volume++) {
		const uint32* dims = volumes[volume].DimsAndFirst;
		const uint64 last_probe = static_cast<uint64>(dims[3]) + dims[0] * dims[1] * dims[2];

		if (last_probe > header.ProbeCount) {
			LogError("Probe file {} has a volume whose probes run past the {} it holds, rebake the probes", path.CStr(),
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
	mVolumeCount = header.VolumeCount;
	mProbeCount = header.ProbeCount;


	RefreshVolumeCounts();

	UploadToGpu(0, mProbeCount);

	LogInfo("Loaded {} light probes across {} volume(s) from {}", mProbeCount, mVolumeCount, path.CStr());
	return true;
}

} // namespace fx
