/*
 * File:        LightProbe.cpp
 * Author:      emd22
 * Created:     10/09/2026
 * Description: All light probe and light probe grid logic. Irradiance probes with baked L2 SH and depth moments for
 * visibility (VSM & DDGI-ish).
 */


#include "LightProbe.hpp"

#include <Asset/AssetManager.hpp>
#include <Core/File.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/BarrierHelper.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

/// Bump whenever the layout or meaning of the cached data changes
#define FX_PROBE_CACHE_FILE_VERSION 7

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

ProbePlacementBoxes GatherPlacementBoxes()
{
	ProbePlacementBoxes boxes;
	uint32 num_objects = 0;

	for (Object& object : gObjectManager->GetCache()) {
		// Only the level's lit geometry. Unlit objects (the sky) and anything attached to the player don't count.
		if (!object.pMesh.IsValid() || object.IsUnlit() || object.GetObjectLayer() == eObjectLayer::PlayerLayer) {
			continue;
		}

		// World space bounds of the object's local bounds, with its rotation and scale applied
		const Mat4f& model_matrix = object.GetModelMatrix();

		Vec3f min(std::numeric_limits<float32>::max());
		Vec3f max(-std::numeric_limits<float32>::max());

		for (uint32 corner = 0; corner < 8; corner++) {
			const Vec4f local((corner & 1) ? object.Bounds.Max.X : object.Bounds.Min.X,
							  (corner & 2) ? object.Bounds.Max.Y : object.Bounds.Min.Y,
							  (corner & 4) ? object.Bounds.Max.Z : object.Bounds.Min.Z, 1.0f);

			const Vec4f world = model_matrix * local;
			const Vec3f point(world.X, world.Y, world.Z);

			min = Vec3f::Min(min, point);
			max = Vec3f::Max(max, point);
		}

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

void SetProbePosition(ProbeInfo& info, const Vec3f& position)
{
	info.ProbePosition[0] = position.X;
	info.ProbePosition[1] = position.Y;
	info.ProbePosition[2] = position.Z;
	info.ProbePosition[3] = 0.0f;
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

struct ProbeFileHeader
{
	char Magic[4] = { 'R', 'P', 'P', 'V' };
	uint32 Version = FX_PROBE_CACHE_FILE_VERSION;
	uint32 ProbeCount = Limits::MaxIrradianceProbes;
};

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
	// matter where the grid sits.
	constexpr float32 cSky[3] = { 0.055f, 0.062f, 0.075f };
	constexpr float32 cGround[3] = { 0.025f, 0.022f, 0.020f };

	std::fill(std::begin(mProbes), std::end(mProbes), MakeSkyGradientProbe(cSky, cGround));

	PlaceGridProbes(Vec3f(-20.0f, -2.0f, -20.0f), Vec3f(150.0f, 15.0f, 150.0f), ProbePlacementBoxes {});
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

	// Pad the volume so that the outermost probes sit off the level's outer surfaces, but never put it below the ground
	Vec3f volume_min = boxes.Min - Vec3f(scVolumePadding);
	volume_min.Y = std::max(volume_min.Y, 0.0f);

	const Vec3f volume_max = boxes.Max + Vec3f(scVolumePadding);

	StartBake(volume_min, volume_max - volume_min, boxes);
}

void ProbeManager::BeginGridBakeAt(const Vec3f& center, const Vec3f& size)
{
	if (IsBaking()) {
		LogWarning("A probe bake is already in progress");
		return;
	}

	StartBake(center - size * 0.5f, size, GatherPlacementBoxes());
}

void ProbeManager::StartBake(const Vec3f& volume_min, const Vec3f& volume_size, const ProbePlacementBoxes& boxes)
{
	PlaceGridProbes(volume_min, volume_size, boxes);

	mCurrentProbe = 0;
	mBakeState = eBakeState::CapturePending;

	LogInfo("Probe grid bake started ({} probes, {} per frame)", Limits::MaxIrradianceProbes, scProbesPerFrame);
}

void ProbeManager::PlaceGridProbes(const Vec3f& volume_min, const Vec3f& volume_size, const ProbePlacementBoxes& boxes)
{
	const uint32* dims = Limits::ProbeGridDims;

	// Probes sit on the volume's boundary, so there are (dim - 1) cells along each axis
	const Vec3f num_cells(static_cast<float32>(dims[0] - 1), static_cast<float32>(dims[1] - 1),
						  static_cast<float32>(dims[2] - 1));

	const Vec3f cell_size = volume_size / num_cells;
	const Vec3f volume_max = volume_min + volume_size;

	const float32 hug_distance = std::min(
		{ scProbeHugMaxDistance, 0.5f * cell_size.X, 0.5f * cell_size.Y, 0.5f * cell_size.Z });

	uint32 num_pushed = 0;
	uint32 num_hugged = 0;
	uint32 probe_index = 0;

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

				SetProbePosition(mProbeInfos[probe_index++], position);
			}
		}
	}

	for (uint32 axis = 0; axis < 3; axis++) {
		mVolume.Min[axis] = volume_min.mData[axis];
		mVolume.InvCellSize[axis] = (volume_size.mData[axis] > 1e-4f)
										? (num_cells.mData[axis] / volume_size.mData[axis])
										: 1.0f;
		mVolume.DimsAndCount[axis] = dims[axis];
	}

	mVolume.Min[3] = 0.0f;
	mVolume.InvCellSize[3] = 0.0f;
	mVolume.DimsAndCount[3] = Limits::MaxIrradianceProbes;

	// The probes have moved, so their depth moments were captured somewhere else. Reset them to "nothing hit" until the
	// bake fills them in, or unbaked probes would report occluders that aren't there.
	for (ProbeInfo& info : mProbeInfos) {
		for (uint32 texel = 0; texel < Limits::ProbeDepthFaces * Limits::ProbeDepthTexelsPerFace; texel++) {
			info.DepthMoments[texel * 2 + 0] = Limits::ProbeDepthMaxDistance;
			info.DepthMoments[texel * 2 + 1] = Limits::ProbeDepthMaxDistance * Limits::ProbeDepthMaxDistance;
		}
	}

	UploadToGpu(0, Limits::MaxIrradianceProbes);

	LogInfo("Probe volume: min={} size={} ({} probes, {} pushed out, {} hugging)", volume_min, volume_size,
			Limits::MaxIrradianceProbes, num_pushed, num_hugged);
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
	const uint32 batch_end = std::min(mCurrentProbe + scProbesPerFrame, Limits::MaxIrradianceProbes);

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

	if (mCurrentProbe < Limits::MaxIrradianceProbes) {
		mBakeState = eBakeState::CapturePending;
		LogInfo("Probe grid bake progress: {}/{}", mCurrentProbe, Limits::MaxIrradianceProbes);
		return;
	}

	mBakeState = eBakeState::Idle;
	LogInfo("Probe grid bake complete ({} probes)", Limits::MaxIrradianceProbes);
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

	UploadRange(graphics->ProbeVolumeBuffer, &mVolume, 0, sizeof(mVolume));
	UploadRange(graphics->ProbeBuffer, mProbes, first_probe * sizeof(ProbeSHData), count * sizeof(ProbeSHData));
	UploadRange(graphics->ProbeDepthBuffer, mProbeInfos, first_probe * sizeof(ProbeInfo), count * sizeof(ProbeInfo));
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

	const ProbeFileHeader header {};

	file.WriteRaw(&header, sizeof(header));
	file.WriteRaw(&mVolume, sizeof(mVolume));
	file.WriteRaw(mProbes, sizeof(mProbes));
	file.WriteRaw(mProbeInfos, sizeof(mProbeInfos));

	LogInfo("Saved {} light probes to {}", Limits::MaxIrradianceProbes, path.CStr());
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

	constexpr uint64 cFileSize = sizeof(ProbeFileHeader) + sizeof(mVolume) + sizeof(mProbes) + sizeof(mProbeInfos);

	const ProbeFileHeader expected_header {};
	ProbeFileHeader header;

	if (file.GetFileSize() != cFileSize || !ReadExact(file, &header, sizeof(header)) ||
		memcmp(&header, &expected_header, sizeof(header)) != 0) {
		LogWarning("Probe file {} is out of date or not a probe file, rebake the probes", path.CStr());
		return false;
	}

	if (!ReadExact(file, &mVolume, sizeof(mVolume)) || !ReadExact(file, mProbes, sizeof(mProbes)) ||
		!ReadExact(file, mProbeInfos, sizeof(mProbeInfos))) {
		LogError("Could not read the probes from {}", path.CStr());
		return false;
	}

	UploadToGpu(0, Limits::MaxIrradianceProbes);

	LogInfo("Loaded {} light probes from {}", Limits::MaxIrradianceProbes, path.CStr());
	return true;
}

} // namespace fx
