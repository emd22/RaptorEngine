/*
 * File:        ReflectionProbe.cpp
 * Author:      emd22
 * Created:     10/09/2026
 * Description: Reflection probes: box brushes whose surroundings are captured into prefiltered cubemaps
 */

#include "ReflectionProbe.hpp"

#include <Asset/AssetManager.hpp>
#include <Blockout.hpp>
#include <CVar.hpp>
#include <Core/File.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Path.hpp>
#include <Engine.hpp>
#include <Math/Float16.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/BarrierHelper.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/ProbePlacement.hpp>
#include <Renderer/ReflectionFilter.hpp>
#include <World.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fx {

namespace {

///////////////////////////////////
// Capture
///////////////////////////////////

float32 TexelToNdc(uint32 index, uint32 size)
{
	return ((static_cast<float32>(index) + 0.5f) / static_cast<float32>(size)) * 2.0f - 1.0f;
}

float32 HalfToRadiance(Float16 value)
{
	constexpr float32 scRadianceClamp = 65000.0f;

	if (value.IsNaN()) {
		return 0.0f;
	}

	return std::clamp(value.ToFloat(), 0.0f, scRadianceClamp);
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

	const FVec3 top = FVec3::Lerp(FVec3(t00), FVec3(t10), fx);
	const FVec3 bottom = FVec3::Lerp(FVec3(t01), FVec3(t11), fx);

	FVec3::Lerp(top, bottom, fy).CopyTo(out);
}

///////////////////////////////////
// Placement
///////////////////////////////////

Blockout* GetBlockout()
{
	if (gWorld != nullptr) {
		return gWorld->pPrototype;
	}

	return nullptr;
}

void CopyHullPlanes(SizedArray<ProbePlacementBoxes::Plane>& planes, const Brush& hull)
{
	planes.InitCapacity(hull.Planes.Size);

	for (uint32 i = 0; i < planes.Capacity; i++) {
		planes.Insert({ hull.Planes[i].Normal, hull.Planes[i].Distance });
	}
}

ProbePlacementBoxes GatherPlacementBoxes()
{
	ProbePlacementBoxes boxes;
	SizedArray<ProbePlacementBoxes::Plane> planes;

	Blockout* blockout = GetBlockout();

	for (Object& object : gObjectManager->GetCache()) {
		const bool is_bound = object.GetSpecialMaterial() == eSpecialMaterial::DBound;

		if (!object.pMesh.IsValid() || object.IsUnlit() ||
			((!object.IsProbeVisible() || object.HasTransparentMaterial()) && !is_bound)) {
			continue;
		}

		// A compiled surface is every face of a material, so its bounds say nothing about where the level is solid
		if (blockout != nullptr && (blockout->IsCompiledObject(&object) || blockout->IsFaceNode(&object))) {
			continue;
		}

		if (object.IsPatch()) {
			const Patch* patch = nullptr;

			if (blockout != nullptr) {
				patch = blockout->GetPatch(&object);
			}

			if (patch != nullptr) {
				ProbePlacement::AddPatchSurfaces(boxes, *patch, object.GetWorldMatrix());
			}

			continue;
		}

		if (object.SubviewInstance != Blockout::scNoSubview && !object.HasTags(eObjectTag::Blockout)) {
			continue;
		}

		const Brush* brush = nullptr;

		if (blockout != nullptr) {
			brush = blockout->GetBrush(&object);
		}

		const bool is_hull = brush != nullptr && brush->IsValid() && !brush->IsBox();

		if (is_hull) {
			CopyHullPlanes(planes, *brush);
		}
		else {
			planes.InitCapacity(0);
		}

		if (is_bound) {
			ProbePlacement::AddBound(boxes, object.Bounds, object.GetWorldMatrix(), planes);
		}
		else {
			ProbePlacement::AddBox(boxes, object.Bounds, object.GetWorldMatrix(), planes);
		}
	}

	if (blockout != nullptr) {
		DynArray<Blockout::SubviewSolid, GrowthFunctions::Double> solids;
		blockout->GetSubviewSolids(solids);

		for (const Blockout::SubviewSolid& solid : solids) {
			if (solid.pOwner->IsUnlit() || !solid.pOwner->IsProbeVisible() || solid.bTransparent) {
				continue;
			}

			const Brush& brush = *solid.pBrush;

			if (brush.IsBox()) {
				planes.InitCapacity(0);
			}
			else {
				CopyHullPlanes(planes, brush);
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

constexpr float32 scReflectionBlendDistance = 1.0f;

constexpr uint64 scReflectionHalfsPerProbe = 6ull * (128 * 128 + 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8 + 4 * 4) * 4;

static_assert(Limits::ReflectionProbeSize == 128 && Limits::ReflectionProbeMips == 6);

struct ReflectionBox
{
	FMat4 BoxToWorld;
	FVec3 HalfExtent;
	float32 Volume;
};

ReflectionBox MakeReflectionBox(const FVec3 local_min, const FVec3 local_max, const FMat4& local_to_world)
{
	constexpr float32 scReflectionMinHalfExtent = 0.05f;

	const FVec3 half_extent = FVec3::Max((local_max - local_min) * 0.5f, FVec3(scReflectionMinHalfExtent));
	const FVec3 center = (local_max + local_min) * 0.5f;

	ReflectionBox box;
	box.BoxToWorld = FMat4::AsScale(half_extent) * FMat4::AsTranslation(center) * local_to_world;
	box.HalfExtent = half_extent;
	box.Volume = half_extent.X * half_extent.Y * half_extent.Z;

	return box;
}

ReflectionProbeData MakeReflectionProbe(const ReflectionBox& box, const ProbePlacementBoxes& boxes, uint32 index)
{
	constexpr float32 scReflectionRelocation = 0.9f;

	const FVec4 center_h = box.BoxToWorld * FVec4(0.0f, 0.0f, 0.0f, 1.0f);
	const FVec3 center(center_h.X, center_h.Y, center_h.Z);

	FVec3 position = center;

	if (!ProbePlacement::IsPositionValid(center, center, boxes) &&
		!ProbePlacement::FindValidPosition(center, center, box.HalfExtent * scReflectionRelocation, boxes, position)) {
		LogWarning("Reflection probe {} is captured from inside of the level's geometry, move its brush", index);
		position = center;
	}

	ReflectionProbeData probe {};
	memcpy(probe.WorldToBox, box.BoxToWorld.AffineInverse().RawData, sizeof(probe.WorldToBox));

	position.CopyTo(probe.PositionAndCount);
	(box.HalfExtent / scReflectionBlendDistance).CopyTo(probe.Fade);

	return probe;
}

///////////////////////////////////
// GPU and file IO
///////////////////////////////////

constexpr uint32 scReflectionFileVersion = 1;

struct ReflectionFileHeader
{
	char Magic[4] = { 'R', 'P', 'R', 'F' };
	uint32 Version = scReflectionFileVersion;
	uint32 Size = Limits::ReflectionProbeSize;
	uint32 Mips = Limits::ReflectionProbeMips;
	uint32 ProbeCount = 0;
};

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

/// Where a mip of a probe starts in its texels, and how wide its faces are
uint64 GetReflectionMipOffset(uint32 mip)
{
	uint64 offset = 0;

	for (uint32 i = 0; i < mip; i++) {
		const uint64 size = Limits::ReflectionProbeSize >> i;
		offset += size * size * Limits::ReflectionProbeFaces * 4;
	}

	return offset;
}

} // namespace

void ReflectionProbeManager::MakeCaptureCameras()
{
	// The cameras are already created, return em
	if (mCaptureCameras.Size > 0) {
		Assert(mCaptureCameras.Size == scCaptureFaces);
		return;
	}

	static const FVec3 scFaceDirections[scCaptureFaces] = {
		FVec3(1.0f, 0.0f, 0.0f),  FVec3(-1.0f, 0.0f, 0.0f), FVec3(0.0f, 1.0f, 0.0f),
		FVec3(0.0f, -1.0f, 0.0f), FVec3(0.0f, 0.0f, 1.0f),	FVec3(0.0f, 0.0f, -1.0f),
	};

	static const FVec3 scFaceUps[scCaptureFaces] = {
		FVec3(0.0f, 1.0f, 0.0f), FVec3(0.0f, 1.0f, 0.0f), FVec3(0.0f, 0.0f, 1.0f),
		FVec3(0.0f, 0.0f, 1.0f), FVec3(0.0f, 1.0f, 0.0f), FVec3(0.0f, 1.0f, 0.0f),
	};

	constexpr float32 scFarClip = 200.0f;
	constexpr float32 scCaptureFOV = 90.0f;

	mCaptureCameras.InitCapacity(scCaptureFaces);

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		PerspectiveCamera& cam = mCaptureCameras.Emplace(MathUtil::DegreesToRadians(scCaptureFOV), 1.0f, scFarClip,
														 ProbePlacement::scCaptureNearPlane);

		// A face's orientation only depends on its direction, so it is set once here. We only move the cameras from
		// this point forward, keeping the orientation.
		cam.LookAt(scFaceDirections[face], scFaceUps[face]);
		cam.Update();
	}
}

void ReflectionProbeManager::MoveCaptureCameras(const FVec3 position)
{
	for (PerspectiveCamera& cam : mCaptureCameras) {
		cam.SetPosition(position);
		cam.Update();
	}
}

void ReflectionProbeManager::Create() { UploadReflectionProbes(0); }

void ReflectionProbeManager::Destroy()
{
	mBakeState = eBakeState::Idle;
	mbCapturingFaces = false;
	mCurrentProbe = 0;

	if (!mbCaptureResourcesCreated) {
		return;
	}

	for (renderer::RawGpuBuffer& staging : mStaging) {
		staging.Destroy();
	}

	mbCaptureResourcesCreated = false;
}

void ReflectionProbeManager::CreateCaptureResources()
{
	if (mbCaptureResourcesCreated) {
		return;
	}

	constexpr VkImageUsageFlags cColorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
											  VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	constexpr VkImageUsageFlags cDepthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	constexpr uint32 cSize = Limits::ReflectionProbeSize;
	constexpr uint64 cPixels = static_cast<uint64>(cSize) * cSize;

	/// The same constant sky the GI probes and the mapc bake use
	constexpr float32 scCaptureSkyRadiance = 1500.0f / static_cast<float32>(M_PI);

	mStage.Create("ReflectionCapture", UVec2(cSize, cSize), eSizeDivisor::FullRes);
	mStage.AddTarget(eImageFormat::RGBA16_Float, cColorUsage, eImageAspectFlag::Color);
	mStage.AddTarget(eImageFormat::D32_Float, cDepthUsage, eImageAspectFlag::Depth);
	mStage.BuildRenderStage();
	mStage.ClearValues[0].color = { { scCaptureSkyRadiance, scCaptureSkyRadiance, scCaptureSkyRadiance, 1.0f } };

	for (renderer::RawGpuBuffer& staging : mStaging) {
		staging.Create(renderer::eGpuBufferType::Transfer, cPixels * sizeof(Float16) * 4, VMA_MEMORY_USAGE_GPU_TO_CPU,
					   eGpuBufferFlags::TransferReceiver);
	}

	MakeCaptureCameras();
	MoveCaptureCameras(FVec3::sZero);

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		const PerspectiveCamera& camera = mCaptureCameras[face];

		mCaptureFaceToClip[face] = (camera.GetInvProjectionMatrix() * camera.GetInvViewMatrix()).Inverse();
	}

	mbCaptureResourcesCreated = true;
}

void ReflectionProbeManager::CopyTargetToStaging(renderer::CommandBuffer& cmd, renderer::RenderStage& stage, uint32 size,
									   eImageFormat format, renderer::RawGpuBuffer& staging)
{
	renderer::Target* target = stage.GetTarget(format);
	Assert(target != nullptr);

	Image& image = target->Image;

	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

	if (format == eImageFormat::D32_Float) {
		aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	renderer::BarrierHelper::ImageLayoutTransition(&image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmd, 0, 1);

	const VkBufferImageCopy copy {
		.imageSubresource { .aspectMask = aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
		.imageExtent { .width = size, .height = size, .depth = 1 },
	};

	vkCmdCopyImageToBuffer(cmd, image.InternalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.Buffer, 1, &copy);

	renderer::BarrierHelper::ImageLayoutTransition(&image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0, 1);
}

FVec3 ReflectionProbeManager::GetReflectionProbePosition(uint32 index) const
{
	return FVec3(mReflectionProbes[index].PositionAndCount);
}

FVec3 ReflectionProbeManager::GetCapturePosition() const
{
	if (mCurrentProbe < mReflectionProbeCount) {
		return GetReflectionProbePosition(mCurrentProbe);
	}

	return FVec3::sZero;
}

uint32 ReflectionProbeManager::RebuildReflectionProbesFromWorld()
{
	if (IsBaking()) {
		LogWarning("Cannot rebuild the reflection probes while they are baking");
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
	uint32 max_brush_probes = Limits::MaxReflectionProbes;

	if (has_level_probe) {
		max_brush_probes -= 1;
	}

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
		const ReflectionBox level_box = MakeReflectionBox(boxes.Min, boxes.Max, FMat4::scIdentity);

		mReflectionProbes[mReflectionProbeCount] = MakeReflectionProbe(level_box, boxes, mReflectionProbeCount);
		mReflectionProbeCount++;
	}

	mbReflectionsBaked = false;

	UploadReflectionProbes(0);

	return mReflectionProbeCount;
}

void ReflectionProbeManager::BeginBake()
{
	if (IsBaking()) {
		LogWarning("A reflection probe bake is already in progress");
		return;
	}

	if (mReflectionProbeCount == 0) {
		LogWarning("No reflection probes in prototype, ignoring request to bake");
		return;
	}

	mCurrentProbe = 0;
	mbReflectionsBaked = false;
	mBakeState = eBakeState::CapturePending;
	mSettleFrames = scSettleFrames;

	mReflectionTexels.Free();
	mReflectionTexels.InitSize(mReflectionProbeCount * scReflectionHalfsPerProbe);
	memset(mReflectionTexels.pData, 0, mReflectionTexels.GetSizeInBytes());

	UploadReflectionProbes(0);

	LogInfo("Reflection probe bake started ({} probes)", mReflectionProbeCount);
}

void ReflectionProbeManager::RecordCapture(renderer::CommandBuffer& cmd, const RenderFaceFunc& render_face)
{
	if (!IsCapturePending()) {
		return;
	}

	CreateCaptureResources();

	mbCapturingFaces = true;

	MoveCaptureCameras(GetReflectionProbePosition(mCurrentProbe));

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		render_face(mCaptureCameras[face], mStage);

		CopyTargetToStaging(cmd, mStage, Limits::ReflectionProbeSize, eImageFormat::RGBA16_Float, mStaging[face]);
	}

	mbCapturingFaces = false;
	mBakeState = eBakeState::CaptureRecorded;
}

void ReflectionProbeManager::ServiceBake()
{
	if (mBakeState == eBakeState::CapturePending && mSettleFrames > 0) {
		mSettleFrames--;
		return;
	}

	if (mBakeState != eBakeState::CaptureRecorded) {
		return;
	}

	renderer::gGraphics->GetDevice()->WaitForIdle();

	const uint32 probe = mCurrentProbe;

	if (!ReadBackReflectionProbe(probe)) {
		LogError("Reflection probe bake failed, could not read probe {}", probe);
		mBakeState = eBakeState::Idle;
		return;
	}

	UploadReflectionCubemap(probe);

	mCurrentProbe++;

	if (mCurrentProbe < mReflectionProbeCount) {
		mBakeState = eBakeState::CapturePending;
		mSettleFrames = scSettleFrames;
		LogInfo("Reflection probe bake progress: {}/{}", mCurrentProbe, mReflectionProbeCount);
		return;
	}

	mBakeState = eBakeState::Idle;
	mbReflectionsBaked = true;

	UploadReflectionProbes(mReflectionProbeCount);

	LogInfo("Reflection probe bake complete ({} probes)", mReflectionProbeCount);
}

bool ReflectionProbeManager::ReadBackReflectionProbe(uint32 probe_index)
{
	constexpr uint32 cSize = Limits::ReflectionProbeSize;
	constexpr uint32 cPixels = cSize * cSize;

	SizedArray<float32> captures[scCaptureFaces];
	bool mapped = true;

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		renderer::RawGpuBuffer& staging = mStaging[face];

		staging.Map();
		staging.InvalidateFromGpu();

		const Float16* rgba = static_cast<const Float16*>(staging.pMappedBuffer);

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
				const FVec3 direction = ReflectionFilter::FaceUVToDirection(face, TexelToNdc(x, cSize),
																			TexelToNdc(y, cSize));

				uint32 capture_face;
				float32 unused_u;
				float32 unused_v;
				ReflectionFilter::DirectionToFaceUV(direction, capture_face, unused_u, unused_v);

				const FVec4 clip = mCaptureFaceToClip[capture_face] *
								   FVec4(direction.X, direction.Y, direction.Z, 1.0f);

				SampleCaptureFace(captures[capture_face].pData, cSize, clip.X / clip.W, clip.Y / clip.W, out);
			}
		}

		cube_faces[face] = cube[face].pData;
	}

	ReflectionFilter::Prefilter(cube_faces, cSize, Limits::ReflectionProbeMips,
								mReflectionTexels.pData + probe_index * scReflectionHalfsPerProbe);

	return true;
}

/// The shader scales each cubemap by the irradiance where it is used over the irradiance at its capture point. That
/// is kept as an ambient cube (one value per axis direction, blended by the squared normal), which unlike a linear
/// fit can't go negative where the light is one-sided. It is gathered from the cubemap itself, in the units of the GI
/// probes (irradiance over pi).
void ReflectionProbeManager::GatherCaptureIrradiance(uint32 probe_index, ReflectionProbeGpu& probe) const
{
	/// Blurred enough to gather from, with few enough texels to do so on every upload
	constexpr uint32 scMip = 2;
	constexpr uint32 cSize = Limits::ReflectionProbeSize >> scMip;

	std::fill(std::begin(probe.CaptureIrradiancePositive), std::end(probe.CaptureIrradiancePositive), 0.0f);
	std::fill(std::begin(probe.CaptureIrradianceNegative), std::end(probe.CaptureIrradianceNegative), 0.0f);

	if (!mbReflectionsBaked || mReflectionTexels.Size < (probe_index + 1) * scReflectionHalfsPerProbe) {
		return;
	}

	const Float16* texel = mReflectionTexels.pData + probe_index * scReflectionHalfsPerProbe +
						   GetReflectionMipOffset(scMip);

	const float32 texel_area = 4.0f / static_cast<float32>(cSize * cSize);

	float32 positive[3] = {};
	float32 negative[3] = {};

	for (uint32 face = 0; face < Limits::ReflectionProbeFaces; face++) {
		for (uint32 y = 0; y < cSize; y++) {
			for (uint32 x = 0; x < cSize; x++, texel += 4) {
				const float32 u = TexelToNdc(x, cSize);
				const float32 v = TexelToNdc(y, cSize);

				const float32 dist_sq = 1.0f + u * u + v * v;
				const float32 solid_angle = texel_area / (dist_sq * sqrtf(dist_sq));

				const float32 luminance = (HalfToRadiance(texel[0]) * 0.2126f) + (HalfToRadiance(texel[1]) * 0.7152f) +
										  (HalfToRadiance(texel[2]) * 0.0722f);

				const FVec3 direction = ReflectionFilter::FaceUVToDirection(face, u, v);
				const float32 axes[3] = { direction.X, direction.Y, direction.Z };

				for (uint32 axis = 0; axis < 3; axis++) {
					const float32 weighted = luminance * solid_angle * fabsf(axes[axis]);

					if (axes[axis] >= 0.0f) {
						positive[axis] += weighted;
					}
					else {
						negative[axis] += weighted;
					}
				}
			}
		}
	}

	for (uint32 axis = 0; axis < 3; axis++) {
		probe.CaptureIrradiancePositive[axis] = positive[axis] / static_cast<float32>(M_PI);
		probe.CaptureIrradianceNegative[axis] = negative[axis] / static_cast<float32>(M_PI);
	}

	probe.CaptureIrradiancePositive[3] = 1.0f;
}

void ReflectionProbeManager::UploadReflectionProbes(uint32 visible_count)
{
	renderer::GraphicsBackend* graphics = renderer::gGraphics;

	graphics->GetDevice()->WaitForIdle();

	ReflectionProbeGpu gpu_probes[Limits::MaxReflectionProbes] {};

	for (uint32 i = 0; i < Limits::MaxReflectionProbes; i++) {
		ReflectionProbeGpu& probe = gpu_probes[i];

		probe.Probe = mReflectionProbes[i];
		probe.Probe.PositionAndCount[3] = static_cast<float32>(visible_count);
		probe.Probe.Fade[3] = static_cast<float32>(mReflectionProbeCount);

		if (i < mReflectionProbeCount) {
			GatherCaptureIrradiance(i, probe);
		}
	}

	UploadRange(graphics->ReflectionProbeBuffer, gpu_probes, 0, sizeof(gpu_probes));
}

void ReflectionProbeManager::UploadReflectionCubemap(uint32 probe_index)
{
	using namespace renderer;

	Image* image = gGraphics->pReflectionProbes;
	Assert(image != nullptr);

	gGraphics->GetDevice()->WaitForIdle();

	const uint64 bytes = scReflectionHalfsPerProbe * sizeof(Float16);

	RawGpuBuffer staging;
	staging.Create(eGpuBufferType::Transfer, bytes, VMA_MEMORY_USAGE_CPU_TO_GPU, eGpuBufferFlags::TransferReceiver);
	staging.Upload(mReflectionTexels.pData + probe_index * scReflectionHalfsPerProbe, bytes);

	VkBufferImageCopy regions[Limits::ReflectionProbeMips];
	uint64 offset = 0;

	for (uint32 mip = 0; mip < Limits::ReflectionProbeMips; mip++) {
		const uint32 mip_size = (Limits::ReflectionProbeSize >> mip);

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

		offset += static_cast<uint64>(mip_size) * mip_size * Limits::ReflectionProbeFaces * 4 * sizeof(Float16);
	}

	gGraphics->SubmitImmediateUploadCmd(
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
// Probe files (.fxrefl)
///////////////////////////////////

bool ReflectionProbeManager::LoadProbes()
{
	if (LoadReflectionProbes()) {
		return true;
	}

	mReflectionProbeCount = 0;
	mbReflectionsBaked = false;
	UploadReflectionProbes(0);

	return false;
}

bool ReflectionProbeManager::SaveProbes()
{
	if (mReflectionProbeCount > 0 && !mbReflectionsBaked) {
		LogWarning("There are reflection probes in the prototype, but none were baked");
		return false;
	}

	const String path = GetProbeFilePath(".fxrefl");

	if (mReflectionProbeCount == 0 && !File(path, File::eModType::Read, File::eDataType::Binary).IsFileOpen()) {
		return true;
	}

	File file(path, File::eModType::Write, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogError("Could not open reflection probes file '{}' for writing", path.CStr());
		return false;
	}

	ReflectionFileHeader header;
	header.ProbeCount = mReflectionProbeCount;

	file.WriteRaw(&header, sizeof(header));
	file.WriteRaw(mReflectionProbes, mReflectionProbeCount * sizeof(ReflectionProbeData));
	file.WriteRaw(mReflectionTexels.pData, mReflectionProbeCount * scReflectionHalfsPerProbe * sizeof(Float16));

	LogInfo("Saved {} reflection probes to '{}'", mReflectionProbeCount, path.CStr());
	return true;
}

bool ReflectionProbeManager::LoadReflectionProbes()
{
	const String path = GetProbeFilePath(".fxrefl");

	File file(path, File::eModType::Read, File::eDataType::Binary);

	if (!file.IsFileOpen()) {
		LogInfo("Could not find reflection probe file at '{}'", path.CStr());
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

	const uint64 texel_bytes = header.ProbeCount * scReflectionHalfsPerProbe * sizeof(Float16);
	const uint64 expected_size = sizeof(header) + header.ProbeCount * sizeof(ReflectionProbeData) + texel_bytes;

	if (file.GetFileSize() != expected_size) {
		LogError("Reflection probe file {} is {} bytes, not the {} its {} probes need", path.CStr(), file.GetFileSize(),
				 expected_size, header.ProbeCount);
		return false;
	}

	ReflectionProbeData probes[Limits::MaxReflectionProbes] {};

	SizedArray<Float16> texels;
	texels.InitSize(header.ProbeCount * scReflectionHalfsPerProbe);

	if (!ReadExact(file, probes, header.ProbeCount * sizeof(ReflectionProbeData)) ||
		!ReadExact(file, texels.pData, texel_bytes)) {
		LogError("Could not read the reflection probes from {}", path.CStr());
		return false;
	}

	for (uint32 probe = 0; probe < header.ProbeCount; probe++) {
		const float32* world_to_box = probes[probe].WorldToBox;

		for (uint32 axis = 0; axis < 3; axis++) {
			const float32 scale = FVec3(world_to_box[axis], world_to_box[4 + axis], world_to_box[8 + axis]).Length();
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
