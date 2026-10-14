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
#include <Core/Path.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/BarrierHelper.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/ReflectionFilter.hpp>
#include <World.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace fx {

namespace {

///////////////////////////////////
// Spherical harmonics
///////////////////////////////////

constexpr float32 scSHNorm0 = 0.282095f;
constexpr float32 scSHNorm1 = 0.488603f;
constexpr float32 scSHNorm2 = 1.092548f;
constexpr float32 scSHNorm20 = 0.315392f;
constexpr float32 scSHNorm22 = 0.546274f;

/// A_l / pi per band, so evaluating the SH gives irradiance / pi
constexpr float32 scCosineLobe[Limits::ProbeSHCoeffCount] = {
	1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f,
};

/// Order must match EvalProbeIrradiance() in Shaders/ProbeCommon.hlsli
void EvalSHBasis(const Vec3f d, float32 out[Limits::ProbeSHCoeffCount])
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

constexpr float32 scRadianceClamp = 65000.0f;
constexpr float32 scCaptureSkyRadiance = 1500.0f / static_cast<float32>(M_PI);

constexpr int64 scDefaultProbeBounces = 3;
constexpr int64 scMaxProbeBounces = 8;

PerspectiveCamera MakeCaptureCamera(const Vec3f position, uint32 face)
{
	static const Vec3f scFaceDirections[ProbeManager::scCaptureFaces] = {
		Vec3f(1.0f, 0.0f, 0.0f),  Vec3f(-1.0f, 0.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f),
		Vec3f(0.0f, -1.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f),	Vec3f(0.0f, 0.0f, -1.0f),
	};

	static const Vec3f scFaceUps[ProbeManager::scCaptureFaces] = {
		Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f),
		Vec3f(0.0f, 0.0f, 1.0f), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f),
	};

	PerspectiveCamera camera(90.0f, 1.0f, 200.0f, ProbePlacement::scCaptureNearPlane);

	camera.Position = position;
	camera.ViewMatrix.LookAt(position, position + scFaceDirections[face], scFaceUps[face]);
	camera.UpdateCameraMatrix();

	return camera;
}

float32 TexelToNdc(uint32 index, uint32 size)
{
	return ((static_cast<float32>(index) + 0.5f) / static_cast<float32>(size)) * 2.0f - 1.0f;
}

uint32 DirectionToMomentTexel(const Vec3f d)
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

/// Subnormals and NaNs decode as zero
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

float32 HalfToRadiance(uint16 half) { return std::clamp(HalfToFloat(half), 0.0f, scRadianceClamp); }

uint16 EncodeUNorm16(float32 value) { return static_cast<uint16>(std::clamp(value, 0.0f, 1.0f) * 65535.0f + 0.5f); }

/// `stored_depth` is reverse-Z, and 0 where nothing was drawn
float32 CaptureDepthToDistance(const Mat4f& inv_projection, float32 stored_depth, float32 ndc_x, float32 ndc_y)
{
	constexpr float32 cFar = Limits::ProbeDepthMaxDistance;

	if (stored_depth <= 0.0f) {
		return cFar;
	}

	const Vec4f view = inv_projection * Vec4f(ndc_x, ndc_y, 1.0f - stored_depth, 1.0f);
	const float32 distance = Vec3f(view.X, view.Y, view.Z).Length() / fabsf(view.W);

	return (distance < cFar) ? distance : cFar;
}

void SampleCaptureFace(const float32* face, uint32 size, float32 ndc_x, float32 ndc_y, float32 out[3])
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

	const float32* t00 = face + (y0 * size + x0) * 3;
	const float32* t10 = face + (y0 * size + x1) * 3;
	const float32* t01 = face + (y1 * size + x0) * 3;
	const float32* t11 = face + (y1 * size + x1) * 3;

	for (uint32 c = 0; c < 3; c++) {
		const float32 top = t00[c] + (t10[c] - t00[c]) * fx;
		const float32 bottom = t01[c] + (t11[c] - t01[c]) * fx;

		out[c] = top + (bottom - top) * fy;
	}
}

///////////////////////////////////
// Placement
///////////////////////////////////

Blockout* GetBlockout() { return (gWorld != nullptr) ? gWorld->pPrototype : nullptr; }

ProbePlacementBoxes GatherPlacementBoxes()
{
	ProbePlacementBoxes boxes;
	SizedArray<ProbePlacementBoxes::Plane> planes;

	Blockout* blockout = GetBlockout();

	for (Object& object : gObjectManager->GetCache()) {
		const bool is_bound = object.IsDBound();

		if (!object.pMesh.IsValid() || object.IsUnlit() || (!object.IsProbeVisible() && !is_bound)) {
			continue;
		}

		// A compiled surface is every face of a material, so its bounds say nothing about where the level is solid
		if (blockout != nullptr && blockout->IsCompiledObject(&object)) {
			continue;
		}

		if (object.SubviewInstance != Blockout::scNoSubview && !object.HasTags(eObjectTag::Blockout)) {
			continue;
		}

		const Brush* brush = (blockout != nullptr) ? blockout->GetBrush(&object) : nullptr;
		const bool is_hull = brush != nullptr && brush->IsValid() && !brush->IsBox();

		planes.InitCapacity(is_hull ? brush->Planes.Size : 0);

		for (uint32 i = 0; i < planes.Capacity; i++) {
			planes.Insert({ brush->Planes[i].Normal, brush->Planes[i].Distance });
		}

		if (is_bound) {
			ProbePlacement::AddBound(boxes, object.Bounds, object.GetWorldMatrix(), planes);
		}
		else {
			ProbePlacement::AddBox(boxes, object.Bounds, object.GetWorldMatrix(), planes);
		}
	}

	if (blockout != nullptr) {
		std::vector<Blockout::SubviewSolid> solids;
		blockout->GetSubviewSolids(solids);

		for (const Blockout::SubviewSolid& solid : solids) {
			if (solid.pOwner->IsUnlit() || !solid.pOwner->IsProbeVisible()) {
				continue;
			}

			const Brush& brush = *solid.pBrush;

			planes.InitCapacity(brush.IsBox() ? 0 : brush.Planes.Size);

			for (uint32 i = 0; i < planes.Capacity; i++) {
				planes.Insert({ brush.Planes[i].Normal, brush.Planes[i].Distance });
			}

			ProbePlacement::AddBox(boxes, BBox(brush.GetBoundsMin(), brush.GetBoundsMax()), solid.World, planes);
		}
	}

	if (boxes.ObjectCount > boxes.Boxes.Size) {
		LogWarning("Probe placement only keeps probes out of the first {} of {} objects", boxes.Boxes.Size,
				   boxes.ObjectCount);
	}

	return boxes;
}

void ResetDepthMoments(ProbeInfo& info)
{
	for (uint32 texel = 0; texel < Limits::ProbeDepthFaces * Limits::ProbeDepthTexelsPerFace; texel++) {
		info.DepthMoments[texel * 2 + 0] = Limits::ProbeDepthMaxDistance;
		info.DepthMoments[texel * 2 + 1] = Limits::ProbeDepthMaxDistance * Limits::ProbeDepthMaxDistance;
	}
}

///////////////////////////////////
// Reflection probes
///////////////////////////////////

constexpr float32 scReflectionBlendDistance = 1.0f;
constexpr float32 scReflectionMinHalfExtent = 0.05f;
constexpr float32 scReflectionRelocation = 0.9f;

constexpr uint64 scReflectionTexelsPerProbe = 6ull * (128 * 128 + 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8 + 4 * 4);
constexpr uint64 scReflectionHalfsPerProbe = scReflectionTexelsPerProbe * 4;

static_assert(Limits::ReflectionProbeSize == 128 && Limits::ReflectionProbeMips == 6);

struct ReflectionBox
{
	Mat4f BoxToWorld;
	Vec3f HalfExtent;
	float32 Volume;
};

ReflectionBox MakeReflectionBox(const Vec3f local_min, const Vec3f local_max, const Mat4f& local_to_world)
{
	const Vec3f half_extent = Vec3f::Max((local_max - local_min) * 0.5f, Vec3f(scReflectionMinHalfExtent));
	const Vec3f center = (local_max + local_min) * 0.5f;

	ReflectionBox box;
	box.BoxToWorld = Mat4f::AsScale(half_extent) * Mat4f::AsTranslation(center) * local_to_world;
	box.HalfExtent = half_extent;
	box.Volume = half_extent.X * half_extent.Y * half_extent.Z;

	return box;
}

ReflectionProbeData MakeReflectionProbe(const ReflectionBox& box, const ProbePlacementBoxes& boxes, uint32 index)
{
	const Vec4f center_h = box.BoxToWorld * Vec4f(0.0f, 0.0f, 0.0f, 1.0f);
	const Vec3f center(center_h.X, center_h.Y, center_h.Z);

	Vec3f position = center;

	if (!ProbePlacement::IsPositionValid(center, center, boxes) &&
		!ProbePlacement::FindValidPosition(center, center, box.HalfExtent * scReflectionRelocation, boxes, position)) {
		LogWarning("Reflection probe {} is captured from inside of the level's geometry, move its brush", index);
		position = center;
	}

	ReflectionProbeData probe {};
	memcpy(probe.WorldToBox, box.BoxToWorld.Inverse().RawData, sizeof(probe.WorldToBox));

	for (uint32 axis = 0; axis < 3; axis++) {
		probe.PositionAndCount[axis] = position.mData[axis];
		probe.Fade[axis] = box.HalfExtent.mData[axis] / scReflectionBlendDistance;
	}

	return probe;
}

///////////////////////////////////
// GPU and file IO
///////////////////////////////////

constexpr uint32 scProbeFileVersion = 13;
constexpr uint32 scReflectionFileVersion = 1;

struct ProbeFileLayout
{
	char Magic[4] = { 'R', 'P', 'P', 'V' };
	uint32 Version = scProbeFileVersion;
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

struct ReflectionFileHeader
{
	char Magic[4] = { 'R', 'P', 'R', 'F' };
	uint32 Version = scReflectionFileVersion;
	uint32 Size = Limits::ReflectionProbeSize;
	uint32 Mips = Limits::ReflectionProbeMips;
	uint32 ProbeCount = 0;
};

uint64 GetProbeFileSize(uint32 probe_count, uint32 grid_point_count)
{
	return sizeof(ProbeFileHeader) + Limits::MaxProbeVolumes * (sizeof(ProbeVolumeData) + sizeof(ProbeVolumeRange)) +
		   static_cast<uint64>(grid_point_count) * sizeof(uint16) +
		   static_cast<uint64>(probe_count) * (sizeof(ProbeSHData) + sizeof(ProbeInfo));
}

String GetProbeFilePath(const char* extension)
{
	if (gWorld != nullptr && gWorld->PrototypePath.GetLength() > 0) {
		return Path(gWorld->PrototypePath).SetExtension(extension).Str();
	}

	return String::Fmt("{}/probes{}", gAssetManager->GetScenePath().CStr(), extension);
}

bool ReadExact(File& file, void* out, uint64 size)
{
	return file.Read2(MakeSlice(static_cast<uint8*>(out), size)) == size;
}

void UploadRange(renderer::RawGpuBuffer& buffer, const void* data, uint64 offset, uint64 size)
{
	uint8* mapped = static_cast<uint8*>(buffer.pMappedBuffer);
	if (mapped == nullptr) {
		return;
	}

	memcpy(mapped + offset, static_cast<const uint8*>(data) + offset, size);
	buffer.FlushToGpu(static_cast<uint32>(offset), static_cast<uint32>(size));
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

	ProbePlacement::AddVolume(mLayout, Vec3f(-20.0f, -2.0f, -20.0f), Vec3f(150.0f, 15.0f, 150.0f),
							  ProbeGridSize { 4, 2, 4 }, ProbePlacementBoxes {}, eProbeFill::Dense);
	ApplyLayout();

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

	mCaptureTexels.Free();

	mbCaptureResourcesCreated = false;
}

///////////////////////////////////
// Placement
///////////////////////////////////

void ProbeManager::GetVolumeProbeRange(uint32 volume, uint32& out_first_probe, uint32& out_count) const
{
	Assert(volume < mLayout.VolumeCount);

	out_first_probe = mLayout.Ranges[volume].FirstProbe;
	out_count = mLayout.Ranges[volume].ProbeCount;
}

void ProbeManager::ApplyLayout()
{
	const uint32 probe_count = GetProbeCount();

	for (uint32 probe = 0; probe < probe_count; probe++) {
		const Vec3f position = mLayout.Positions[probe];
		ProbeInfo& info = mProbeInfos[probe];

		info.ProbePosition[0] = position.X;
		info.ProbePosition[1] = position.Y;
		info.ProbePosition[2] = position.Z;
		info.ProbePosition[3] = 1.0f;

		ResetDepthMoments(info);
	}

	mbGridDirty = true;
	mbSunVisibilityDirty = true;

	UploadToGpu(0, probe_count);
}

uint32 ProbeManager::RebuildVolumesFromWorld()
{
	if (IsBaking()) {
		LogWarning("Cannot rebuild the probe volumes while they are baking");
		return 0;
	}

	Blockout* blockout = GetBlockout();
	const ProbeLayout* compiled_layout = (blockout != nullptr) ? blockout->GetCompiledProbeLayout() : nullptr;

	uint32 num_brush_volumes = 0;

	if (compiled_layout != nullptr) {
		mLayout.CopyFrom(*compiled_layout);
	}
	else {
		const ProbePlacementBoxes boxes = GatherPlacementBoxes();

		if (boxes.IsEmpty()) {
			mLayout.Clear();
			ApplyLayout();

			LogError("Cannot fit a probe volume to the level: there is no geometry to fit it to");
			return 0;
		}

		DynArray<BBox, GrowthFunctions::Double> brush_regions;

		for (Object& object : gObjectManager->GetCache()) {
			if (object.IsProbeVolume() && !object.IsReflectionProbe()) {
				brush_regions.Emplace(object.GetWorldOBB().GetWorldAABB());
			}
		}

		ProbePlacementSettings settings;
		settings.BrushSpacing = gCVars->Get("r_probe_spacing", ProbePlacementSettings::scDefaultBrushSpacing);
		settings.LevelSpacing = gCVars->Get("r_probe_level_spacing", ProbePlacementSettings::scDefaultLevelSpacing);

		num_brush_volumes = ProbePlacement::BuildLevelLayout(
			mLayout, boxes, Slice<BBox>(brush_regions.pData, brush_regions.Size), settings);
	}

	ApplyLayout();

	RebuildReflectionProbesFromWorld();

	LogInfo("Probe volumes {}: {} volumes ({} from editor brushes), {} of {} probes and {} of {} grid points used",
			(compiled_layout != nullptr) ? "taken from the compiled map" : "rebuilt", mLayout.VolumeCount,
			num_brush_volumes, GetProbeCount(), Limits::MaxIrradianceProbes, mLayout.GetGridPointCount(),
			Limits::MaxProbeGridPoints);

	return num_brush_volumes;
}

///////////////////////////////////
// Sun visibility
///////////////////////////////////

void ProbeManager::SetSunVisibility(Slice<const Vec3f> positions, Slice<const uint8> values)
{
	const uint32 count = static_cast<uint32>(std::min(positions.Size, values.Size));

	mSunSamplePositions.Assign(positions.pData, count);
	mSunSampleValues.Assign(values.pData, count);
	mbSunVisibilityDirty = true;

	UploadToGpu(0, GetProbeCount());
}

void ProbeManager::ClearSunVisibility()
{
	mSunSamplePositions.Clear();
	mSunSampleValues.Clear();
	mbSunVisibilityDirty = true;

	UploadToGpu(0, GetProbeCount());
}

void ProbeManager::ResolveSunVisibility()
{
	constexpr float32 cSamePlace = 1e-4f;

	const uint32 sample_count = mSunSamplePositions.Size;

	for (uint32 probe = 0; probe < GetProbeCount(); probe++) {
		if (sample_count == 0) {
			mSunVisibility[probe] = 1.0f;
			continue;
		}

		const Vec3f position = mLayout.Positions[probe];

		uint32 nearest = std::min(probe, sample_count - 1);
		float32 nearest_distance = position.DistanceTo(mSunSamplePositions[nearest]);

		for (uint32 sample = 0; sample < sample_count && nearest_distance > cSamePlace; sample++) {
			const float32 distance = position.DistanceTo(mSunSamplePositions[sample]);

			if (distance < nearest_distance) {
				nearest_distance = distance;
				nearest = sample;
			}
		}

		mSunVisibility[probe] = static_cast<float32>(mSunSampleValues[nearest]) / 255.0f;
	}

	mbSunVisibilityDirty = false;
}

///////////////////////////////////
// Irradiance bake
///////////////////////////////////

void ProbeManager::BeginBake()
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	if (GetProbeCount() == 0) {
		LogError("Probe bake failed: no probes have been placed");
		return;
	}

	mCurrentProbe = 0;
	mCurrentBounce = 0;
	mBakePhase = eBakePhase::Irradiance;
	mBounceCount = static_cast<uint32>(
		std::clamp<int64>(gCVars->Get("r_probe_bounces", scDefaultProbeBounces), 1, scMaxProbeBounces));
	mBakeState = eBakeState::CapturePending;

	LogInfo("Probe bake started ({} probes across {} volume(s), {} per frame, {} bounce(s))", GetProbeCount(),
			mLayout.VolumeCount, scProbesPerFrame, mBounceCount);
}

void ProbeManager::CreateCaptureResources()
{
	if (mbCaptureResourcesCreated) {
		return;
	}

	constexpr VkImageUsageFlags cColorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
											  VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	constexpr VkImageUsageFlags cDepthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	constexpr uint32 cReflectionSize = Limits::ReflectionProbeSize;
	constexpr uint64 cPixels = static_cast<uint64>(scCaptureSize) * scCaptureSize;
	constexpr uint64 cReflectionPixels = static_cast<uint64>(cReflectionSize) * cReflectionSize;

	const VkClearColorValue sky_clear = { { scCaptureSkyRadiance, scCaptureSkyRadiance, scCaptureSkyRadiance, 1.0f } };

	const auto create_staging = [](renderer::RawGpuBuffer& staging, uint64 size)
	{
		staging.Create(renderer::eGpuBufferType::Transfer, size, VMA_MEMORY_USAGE_GPU_TO_CPU,
					   eGpuBufferFlags::TransferReceiver);
	};

	mCaptureStage.Create("ProbeCapture", Vec2u(scCaptureSize, scCaptureSize), eSizeDivisor::FullRes);
	mCaptureStage.AddTarget(eImageFormat::RGBA16_Float, cColorUsage, eImageAspectFlag::Color);
	mCaptureStage.AddTarget(eImageFormat::D32_Float, cDepthUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
							eImageAspectFlag::Depth);
	mCaptureStage.BuildRenderStage();
	mCaptureStage.ClearValues[0].color = sky_clear;

	for (uint32 slot = 0; slot < scProbesPerFrame; slot++) {
		for (uint32 face = 0; face < scCaptureFaces; face++) {
			create_staging(mColorStaging[slot][face], cPixels * sizeof(uint16) * 4);
			create_staging(mDepthStaging[slot][face], cPixels * sizeof(float32));
		}
	}

	mReflectionStage.Create("ReflectionCapture", Vec2u(cReflectionSize, cReflectionSize), eSizeDivisor::FullRes);
	mReflectionStage.AddTarget(eImageFormat::RGBA16_Float, cColorUsage, eImageAspectFlag::Color);
	mReflectionStage.AddTarget(eImageFormat::D32_Float, cDepthUsage, eImageAspectFlag::Depth);
	mReflectionStage.BuildRenderStage();
	mReflectionStage.ClearValues[0].color = sky_clear;

	for (renderer::RawGpuBuffer& staging : mReflectionStaging) {
		create_staging(staging, cReflectionPixels * sizeof(uint16) * 4);
	}

	BuildCaptureTexels();

	mbCaptureResourcesCreated = true;
}

void ProbeManager::BuildCaptureTexels()
{
	constexpr float32 cTexelArea = 4.0f / static_cast<float32>(scCaptureSize * scCaptureSize);

	mCaptureTexels.InitSize(scCaptureFaces * scCaptureSize * scCaptureSize);
	CaptureTexel* texel = mCaptureTexels.pData;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		const PerspectiveCamera camera = MakeCaptureCamera(Vec3f::sZero, face);
		const Mat4f inv_view_projection = camera.InvProjectionMatrix * camera.InvViewMatrix;

		mCaptureInvProjection = camera.InvProjectionMatrix;
		mCaptureFaceToClip[face] = inv_view_projection.Inverse();

		for (uint32 y = 0; y < scCaptureSize; y++) {
			for (uint32 x = 0; x < scCaptureSize; x++, texel++) {
				const float32 ndc_x = TexelToNdc(x, scCaptureSize);
				const float32 ndc_y = TexelToNdc(y, scCaptureSize);

				const Vec4f point = inv_view_projection * Vec4f(ndc_x, ndc_y, 0.5f, 1.0f);
				const Vec3f direction = Vec3f(point.X / point.W, point.Y / point.W, point.Z / point.W).Normalize();

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

	if (mBakePhase == eBakePhase::Reflection) {
		RecordReflectionCapture(cmd, render_face);
		return;
	}

	mBatchStart = mCurrentProbe;
	const uint32 batch_end = std::min(mCurrentProbe + scProbesPerFrame, GetProbeCount());

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
	renderer::Target* target = stage.GetTarget(format);
	Assert(target != nullptr);

	Image& image = target->Image;

	const VkImageAspectFlags aspect = (format == eImageFormat::D32_Float) ? VK_IMAGE_ASPECT_DEPTH_BIT
																		  : VK_IMAGE_ASPECT_COLOR_BIT;

	renderer::BarrierHelper::ImageLayoutTransition(&image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmd, 0, 1);

	const VkBufferImageCopy copy {
		.imageSubresource { .aspectMask = aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
		.imageExtent { .width = size, .height = size, .depth = 1 },
	};

	vkCmdCopyImageToBuffer(cmd, image.InternalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.Buffer, 1, &copy);

	renderer::BarrierHelper::ImageLayoutTransition(&image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0, 1);
}

void ProbeManager::ServiceCaptureBake()
{
	if (mBakeState != eBakeState::CaptureRecorded) {
		return;
	}

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

	if (mCurrentProbe < GetProbeCount()) {
		mBakeState = eBakeState::CapturePending;
		LogInfo("Probe bake progress: {}/{}", mCurrentProbe, GetProbeCount());
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
	LogInfo("Probe bake complete ({} probes across {} volume(s), {} bounce(s))", GetProbeCount(), mLayout.VolumeCount,
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
		renderer::RawGpuBuffer& color = mColorStaging[batch_slot][face];
		renderer::RawGpuBuffer& depth = mDepthStaging[batch_slot][face];

		color.Map();
		color.InvalidateFromGpu();
		depth.Map();
		depth.InvalidateFromGpu();

		colors[face] = static_cast<const uint16*>(color.pMappedBuffer);
		depths[face] = static_cast<const float32*>(depth.pMappedBuffer);

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
					const float32 radiance = HalfToRadiance(rgba[c]);

					for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
						sh[k][c] += radiance * texel->WeightedBasis[k];
					}
				}

				const float32 distance = CaptureDepthToDistance(mCaptureInvProjection, depths[face][pixel],
																TexelToNdc(x, scCaptureSize),
																TexelToNdc(y, scCaptureSize));

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
			mean_sq = std::max(moment_sum_sq[t] / moment_weight[t], mean * mean);
		}

		out_info.DepthMoments[t * 2 + 0] = mean;
		out_info.DepthMoments[t * 2 + 1] = mean_sq;
	}
}

///////////////////////////////////
// GPU upload
///////////////////////////////////

void ProbeManager::UploadToGpu(uint32 first_probe, uint32 count)
{
	renderer::GraphicsBackend* graphics = renderer::gGraphics;

	graphics->GetDevice()->WaitForIdle();

	// The shader reads the volume count out of whichever entry it has
	for (ProbeVolumeData& volume : mLayout.Volumes) {
		volume.MinAndCount[3] = static_cast<float32>(mLayout.VolumeCount);
	}

	UploadRange(graphics->ProbeVolumeBuffer, mLayout.Volumes, 0, sizeof(mLayout.Volumes));

	if (mbGridDirty && mLayout.Grid.Size > 0) {
		UploadRange(graphics->ProbeGridBuffer, mLayout.Grid.pData, 0, mLayout.Grid.Size * sizeof(uint16));
	}

	mbGridDirty = false;

	if (mbSunVisibilityDirty) {
		ResolveSunVisibility();

		first_probe = 0;
		count = GetProbeCount();
	}

	if (count == 0) {
		return;
	}

	for (uint32 probe = first_probe; probe < first_probe + count; probe++) {
		for (uint32 lane = 0; lane < 3; lane++) {
			mProbes[probe].SH[lane][3] = mProbeInfos[probe].ProbePosition[lane];
		}

		mProbes[probe].SH[3][3] = mSunVisibility[probe];
	}

	UploadRange(graphics->ProbeBuffer, mProbes, first_probe * sizeof(ProbeSHData), count * sizeof(ProbeSHData));
	UploadMomentsAtlas(first_probe, count);
}

void ProbeManager::UploadMomentsAtlas(uint32 first_probe, uint32 count)
{
	using namespace renderer;

	constexpr uint32 cFaceSize = Limits::ProbeDepthSize;

	GraphicsBackend* graphics = gGraphics;
	Assert(graphics->pProbeMomentsAtlas != nullptr);

	const uint32 first_row = first_probe / Limits::ProbeAtlasColumns;
	const uint32 last_row = (first_probe + count - 1) / Limits::ProbeAtlasColumns;

	SizedArray<uint16> row;
	row.InitSize(static_cast<uint64>(Limits::ProbeAtlasWidth) * cFaceSize * 2);

	for (uint32 atlas_row = first_row; atlas_row <= last_row; atlas_row++) {
		// Unused probes keep the max distance, which reads as unoccluded
		memset(row.pData, 0xFF, row.GetSizeInBytes());

		const uint32 row_first_probe = atlas_row * Limits::ProbeAtlasColumns;
		const uint32 row_end_probe = std::min(row_first_probe + Limits::ProbeAtlasColumns, GetProbeCount());

		for (uint32 probe = row_first_probe; probe < row_end_probe; probe++) {
			const float32* moments = mProbeInfos[probe].DepthMoments;
			const uint32 strip_x = (probe - row_first_probe) * Limits::ProbeAtlasProbeWidth;

			for (uint32 face = 0; face < Limits::ProbeDepthFaces; face++) {
				for (uint32 y = 0; y < cFaceSize; y++) {
					for (uint32 x = 0; x < cFaceSize; x++) {
						const uint32 src = (face * Limits::ProbeDepthTexelsPerFace + y * cFaceSize + x) * 2;
						const uint32 dst = (y * Limits::ProbeAtlasWidth + strip_x + face * cFaceSize + x) * 2;

						// Stored as a standard deviation, mean_sq - mean^2 would cancel to nothing at 16 bits
						const float32 mean = moments[src + 0];
						const float32 stddev = std::sqrt(std::max(moments[src + 1] - (mean * mean), 0.0f));

						row[dst + 0] = EncodeUNorm16(mean / Limits::ProbeDepthMaxDistance);
						row[dst + 1] = EncodeUNorm16(stddev / Limits::ProbeDepthMaxDistance);
					}
				}
			}
		}

		RawGpuBuffer staging;
		staging.Create(eGpuBufferType::Transfer, row.GetSizeInBytes(), VMA_MEMORY_USAGE_CPU_TO_GPU,
					   eGpuBufferFlags::TransferReceiver);
		staging.Upload(row.pData, row.GetSizeInBytes());

		const Vec2u row_size(Limits::ProbeAtlasWidth, cFaceSize);
		const Vec2u row_offset(0, atlas_row * cFaceSize);

		graphics->SubmitImmediateUploadCmd(
			[&](CommandBuffer& cmd)
			{
				graphics->pProbeMomentsAtlas->CopyFromBuffer(cmd, staging, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
															 row_size, 0, 0, row_offset);
			});

		staging.Destroy();
	}
}

///////////////////////////////////
// Reflection probes
///////////////////////////////////

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

	DynArray<ReflectionBox, GrowthFunctions::Double> reflection_boxes;

	for (Object& object : gObjectManager->GetCache()) {
		if (object.IsReflectionProbe()) {
			reflection_boxes.Emplace(MakeReflectionBox(object.Bounds.Min, object.Bounds.Max, object.GetWorldMatrix()));
		}
	}

	std::sort(reflection_boxes.begin(), reflection_boxes.end(),
			  [](const ReflectionBox& a, const ReflectionBox& b) { return a.Volume < b.Volume; });

	const bool has_level_probe = !boxes.IsEmpty() && gCVars->Get("r_reflection_level_probe", int64(0)) != 0;
	const uint32 max_brush_probes = Limits::MaxReflectionProbes - (has_level_probe ? 1 : 0);
	const uint32 num_brush_probes = std::min(reflection_boxes.Size, max_brush_probes);

	if (reflection_boxes.Size > max_brush_probes) {
		LogWarning("{} reflection probe brushes are placed but only {} fit, the largest are left out",
				   reflection_boxes.Size, max_brush_probes);
	}

	for (uint32 i = 0; i < num_brush_probes; i++) {
		mReflectionProbes[i] = MakeReflectionProbe(reflection_boxes[i], boxes, i);
	}

	mReflectionProbeCount = num_brush_probes;

	if (has_level_probe) {
		const ReflectionBox level_box = MakeReflectionBox(boxes.Min, boxes.Max, Mat4f::scIdentity);

		mReflectionProbes[mReflectionProbeCount] = MakeReflectionProbe(level_box, boxes, mReflectionProbeCount);
		mReflectionProbeCount++;
	}

	mbReflectionsBaked = false;

	UploadReflectionProbes(0);

	LogInfo("Reflection probes rebuilt: {} from editor brushes, {} in total", num_brush_probes, mReflectionProbeCount);

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

	mReflectionTexels.Free();
	mReflectionTexels.InitSize(mReflectionProbeCount * scReflectionHalfsPerProbe);
	memset(mReflectionTexels.pData, 0, mReflectionTexels.GetSizeInBytes());

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

	SizedArray<float32> captures[scCaptureFaces];
	bool mapped = true;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		renderer::RawGpuBuffer& staging = mReflectionStaging[face];

		staging.Map();
		staging.InvalidateFromGpu();

		const uint16* rgba = static_cast<const uint16*>(staging.pMappedBuffer);

		if (rgba == nullptr) {
			mapped = false;
		}
		else {
			captures[face].InitSize(cPixels * 3);

			for (uint32 pixel = 0; pixel < cPixels; pixel++) {
				for (uint32 c = 0; c < 3; c++) {
					captures[face][pixel * 3 + c] = HalfToRadiance(rgba[pixel * 4 + c]);
				}
			}
		}

		staging.UnMap();
	}

	if (!mapped) {
		return false;
	}

	// The captures are perspective views, the filter wants cube faces in its own face layout
	SizedArray<float32> cube[scCaptureFaces];
	const float32* cube_faces[scCaptureFaces];

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		cube[face].InitSize(cPixels * 3);
		float32* out = cube[face].pData;

		for (uint32 y = 0; y < cSize; y++) {
			for (uint32 x = 0; x < cSize; x++, out += 3) {
				const Vec3f direction = ReflectionFilter::FaceUVToDirection(face, TexelToNdc(x, cSize),
																			TexelToNdc(y, cSize));

				uint32 capture_face;
				float32 unused_u;
				float32 unused_v;
				ReflectionFilter::DirectionToFaceUV(direction, capture_face, unused_u, unused_v);

				const Vec4f clip = mCaptureFaceToClip[capture_face] *
								   Vec4f(direction.X, direction.Y, direction.Z, 1.0f);

				SampleCaptureFace(captures[capture_face].pData, cSize, clip.X / clip.W, clip.Y / clip.W, out);
			}
		}

		cube_faces[face] = cube[face].pData;
	}

	ReflectionFilter::Prefilter(cube_faces, cSize, Limits::ReflectionProbeMips,
								mReflectionTexels.pData + probe_index * scReflectionHalfsPerProbe);

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
	staging.Create(eGpuBufferType::Transfer, bytes, VMA_MEMORY_USAGE_CPU_TO_GPU, eGpuBufferFlags::TransferReceiver);
	staging.Upload(mReflectionTexels.pData + probe_index * scReflectionHalfsPerProbe, bytes);

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

			vkCmdCopyBufferToImage(cmd, staging.Buffer, image->InternalImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
								   Limits::ReflectionProbeMips, regions);

			BarrierHelper::ImageLayoutTransition(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0,
												 Limits::ReflectionProbeMips);
		});

	staging.Destroy();
}

///////////////////////////////////
// Probe files (.fxprobe, .fxrefl)
///////////////////////////////////

bool ProbeManager::SaveProbes()
{
	if (!SaveIrradianceProbes()) {
		return false;
	}

	SaveReflectionProbes();
	return true;
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

	const String path = GetProbeFilePath(".fxprobe");

	File file(path, File::eModType::Write, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogError("Could not open {} to save the probes", path.CStr());
		return false;
	}

	ProbeFileHeader header;
	header.VolumeCount = mLayout.VolumeCount;
	header.ProbeCount = GetProbeCount();
	header.GridPointCount = mLayout.GetGridPointCount();

	file.WriteRaw(&header, sizeof(header));
	file.WriteRaw(mLayout.Volumes, sizeof(mLayout.Volumes));
	file.WriteRaw(mLayout.Ranges, sizeof(mLayout.Ranges));
	file.WriteRaw(mLayout.Grid.pData, header.GridPointCount * sizeof(uint16));
	file.WriteRaw(mProbes, header.ProbeCount * sizeof(ProbeSHData));
	file.WriteRaw(mProbeInfos, header.ProbeCount * sizeof(ProbeInfo));

	LogInfo("Saved {} light probes across {} volume(s) to {}", header.ProbeCount, header.VolumeCount, path.CStr());
	return true;
}

bool ProbeManager::LoadIrradianceProbes()
{
	const String path = GetProbeFilePath(".fxprobe");

	File file(path, File::eModType::Read, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogInfo("No probe file at {}, using procedural probes", path.CStr());
		return false;
	}

	const ProbeFileLayout expected {};
	ProbeFileHeader header;

	if (!ReadExact(file, &header, sizeof(header)) || memcmp(&header.Layout, &expected, sizeof(expected)) != 0) {
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

	ProbeLayout layout;
	layout.VolumeCount = header.VolumeCount;

	uint16* grid = layout.AddGridPoints(header.GridPointCount);

	if (!ReadExact(file, layout.Volumes, sizeof(layout.Volumes)) ||
		!ReadExact(file, layout.Ranges, sizeof(layout.Ranges)) ||
		!ReadExact(file, grid, header.GridPointCount * sizeof(uint16))) {
		LogError("Could not read the probe volumes from {}", path.CStr());
		return false;
	}

	for (uint32 volume = 0; volume < header.VolumeCount; volume++) {
		const uint32* dims = layout.Volumes[volume].DimsAndFirst;
		const ProbeVolumeRange& range = layout.Ranges[volume];

		const uint64 last_point = static_cast<uint64>(dims[3]) + static_cast<uint64>(dims[0]) * dims[1] * dims[2];
		const uint64 last_probe = static_cast<uint64>(range.FirstProbe) + range.ProbeCount;

		if (last_point > header.GridPointCount || last_probe > header.ProbeCount) {
			LogError("Probe file {} has a volume that runs past the probes it holds, rebake the probes", path.CStr());
			return false;
		}
	}

	for (const uint16 probe : layout.Grid) {
		if (probe < Limits::ProbeGridSolid && probe >= header.ProbeCount) {
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

	for (uint32 probe = 0; probe < header.ProbeCount; probe++) {
		const float32* position = mProbeInfos[probe].ProbePosition;
		layout.Positions.Emplace(Vec3f(position[0], position[1], position[2]));
	}

	mLayout = std::move(layout);
	mbGridDirty = true;
	mbSunVisibilityDirty = true;

	UploadToGpu(0, header.ProbeCount);

	LogInfo("Loaded {} light probes across {} volume(s) from {}", header.ProbeCount, header.VolumeCount, path.CStr());
	return true;
}

bool ProbeManager::SaveReflectionProbes()
{
	if (mReflectionProbeCount > 0 && !mbReflectionsBaked) {
		LogWarning("The reflection probes are not baked, so they were not saved");
		return false;
	}

	const String path = GetProbeFilePath(".fxrefl");

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
	file.WriteRaw(mReflectionTexels.pData, mReflectionProbeCount * scReflectionHalfsPerProbe * sizeof(uint16));

	LogInfo("Saved {} reflection probes to {}", mReflectionProbeCount, path.CStr());
	return true;
}

bool ProbeManager::LoadReflectionProbes()
{
	const String path = GetProbeFilePath(".fxrefl");

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

	SizedArray<uint16> texels;
	texels.InitSize(header.ProbeCount * scReflectionHalfsPerProbe);

	if (!ReadExact(file, probes, header.ProbeCount * sizeof(ReflectionProbeData)) ||
		!ReadExact(file, texels.pData, texel_bytes)) {
		LogError("Could not read the reflection probes from {}", path.CStr());
		return false;
	}

	for (uint32 probe = 0; probe < header.ProbeCount; probe++) {
		const float32* world_to_box = probes[probe].WorldToBox;

		for (uint32 axis = 0; axis < 3; axis++) {
			const float32 scale = Vec3f(world_to_box[axis], world_to_box[4 + axis], world_to_box[8 + axis]).Length();
			probes[probe].Fade[axis] = 1.0f / (std::max(scale, 1e-6f) * scReflectionBlendDistance);
		}
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
