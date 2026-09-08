/*
 * File:        LightProbe.cpp
 * Description: MVP light probe manager (SH L2 diffuse irradiance).
 */

#include "LightProbe.hpp"

#include <Asset/AssetManager.hpp>
#include <Core/File.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <World.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>

namespace fx {

// SH normalization constants (Peter-Pike Sloan). Basis order must match
// EvalProbeIrradiance() in Shaders/ProbeCommon.hlsli.
static constexpr float32 scY00 = 0.282095f;
static constexpr float32 scY1 = 0.488603f;

ProbeData MakeUniformAmbientProbe(float32 r, float32 g, float32 b)
{
	ProbeData probe {};
	// Constant irradiance E(n) = A  =>  L00 = A / Y00, all other coeffs zero.
	probe.SH[0][0] = r / scY00;
	probe.SH[0][1] = g / scY00;
	probe.SH[0][2] = b / scY00;
	return probe;
}

ProbeData MakeSkyGradientProbe(const float32 sky[3], const float32 ground[3])
{
	ProbeData probe {};
	// E(n) ~= avg + slope * n.y, with avg = (sky+ground)/2, slope = (sky-ground)/2.
	// L00 = avg / Y00, L(Y1-1, the y-basis coeff at index 1) = slope / 0.488603.
	for (uint32 c = 0; c < 3; c++) {
		const float32 avg = (sky[c] + ground[c]) * 0.5f;
		const float32 slope = (sky[c] - ground[c]) * 0.5f;
		probe.SH[0][c] = avg / scY00;
		probe.SH[1][c] = slope / scY1;
	}
	return probe;
}

void ProbeManager::Create()
{
	if (mbInitialized) {
		return;
	}

	// All probes are always valid data; the volume descriptor selects the
	// blend. Default to a uniform gradient everywhere.
	const float32 sky[3] = { 0.055f, 0.062f, 0.075f };
	const float32 ground[3] = { 0.025f, 0.022f, 0.020f };
	const ProbeData def = MakeSkyGradientProbe(sky, ground);
	for (uint32 i = 0; i < Limits::MaxIrradianceProbes; i++) {
		mProbes[i] = def;
	}

	// Default volume: 4x2x4 grid over a 40x10x40m region at origin. Replaced by
	// ComputeGridPlacement() on the first grid bake.
	mVolume.Min[0] = -20.0f;
	mVolume.Min[1] = -2.0f;
	mVolume.Min[2] = -20.0f;
	mVolume.Min[3] = 0.0f;
	mVolume.InvCellSize[0] = 1.0f / 10.0f;
	mVolume.InvCellSize[1] = 1.0f / 5.0f;
	mVolume.InvCellSize[2] = 1.0f / 10.0f;
	mVolume.InvCellSize[3] = 0.0f;
	mVolume.DimsAndCount[0] = Limits::ProbeGridDims[0];
	mVolume.DimsAndCount[1] = Limits::ProbeGridDims[1];
	mVolume.DimsAndCount[2] = Limits::ProbeGridDims[2];
	mVolume.DimsAndCount[3] = Limits::MaxIrradianceProbes;

	UploadToGpu();
	UploadVolumeToGpu();

	mbInitialized = true;
}

void ProbeManager::Destroy()
{
	mbInitialized = false;
	mbCapturePending = false;
	mbCaptureReady = false;
	mNumProbesPending = 0;
	mCurrentProbe = 0;

	for (uint32 i = 0; i < scCaptureFaces; i++) {
		mCaptureStaging[i].Destroy();
	}
}

void ProbeManager::SetUniformAmbient(float32 r, float32 g, float32 b)
{
	const ProbeData probe = MakeUniformAmbientProbe(r, g, b);
	for (uint32 i = 0; i < Limits::MaxIrradianceProbes; i++) {
		mProbes[i] = probe;
	}
	UploadToGpu();
}

void ProbeManager::SetSkyGradient(const float32 sky[3], const float32 ground[3])
{
	const ProbeData probe = MakeSkyGradientProbe(sky, ground);
	for (uint32 i = 0; i < Limits::MaxIrradianceProbes; i++) {
		mProbes[i] = probe;
	}
	UploadToGpu();
}

/// SH basis matching EvalProbeIrradiance() in Shaders/ProbeCommon.hlsli.
/// Order: Y00, Y1-1(y), Y10(z), Y11(x), Y2-2(xy), Y2-1(yz), Y20, Y21(xz), Y22.
static void ProbeBasisSH(float32 x, float32 y, float32 z, float32 out_basis[9])
{
	out_basis[0] = 0.282095f;
	out_basis[1] = 0.488603f * y;
	out_basis[2] = 0.488603f * z;
	out_basis[3] = 0.488603f * x;
	out_basis[4] = 1.092548f * x * y;
	out_basis[5] = 1.092548f * y * z;
	out_basis[6] = 0.315392f * (3.0f * z * z - 1.0f);
	out_basis[7] = 1.092548f * x * z;
	out_basis[8] = 0.546274f * (x * x - y * y);
}

void ProbeManager::BakeFromSceneLights(const Vec3f& sunDir, const float32 sunRGB[3], const float32 ambRGB[3])
{
	// Fibonacci sphere sampling for an even directional distribution.
	static constexpr uint32 scSampleCount = 1024;
	static constexpr float32 scGoldenAngle = 2.3999632f;
	static constexpr float32 scFourPiOverN = (4.0f * 3.14159265f) / static_cast<float32>(scSampleCount);

	const Vec3f sun = sunDir.Normalize();

	float32 sh[Limits::ProbeSHCoeffCount][3] = {};

	for (uint32 i = 0; i < scSampleCount; i++) {
		const float32 y = 1.0f - (2.0f * (static_cast<float32>(i) + 0.5f) / static_cast<float32>(scSampleCount));
		const float32 r = sqrtf(fmaxf(1.0f - y * y, 0.0f));
		const float32 phi = static_cast<float32>(i) * scGoldenAngle;

		const float32 x = r * cosf(phi);
		const float32 z = r * sinf(phi);

		const float32 n_dot_l = fmaxf(x * sun.X + y * sun.Y + z * sun.Z, 0.0f);

		float32 basis[Limits::ProbeSHCoeffCount];
		ProbeBasisSH(x, y, z, basis);

		for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
			for (uint32 c = 0; c < 3; c++) {
				sh[k][c] += (ambRGB[c] + sunRGB[c] * n_dot_l) * basis[k];
			}
		}
	}

	ProbeData probe {};
	for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
		for (uint32 c = 0; c < 3; c++) {
			probe.SH[k][c] = sh[k][c] * scFourPiOverN;
		}
		probe.SH[k][3] = 0.0f;
	}

	for (uint32 i = 0; i < Limits::MaxIrradianceProbes; i++) {
		mProbes[i] = probe;
	}

	UploadToGpu();
}

///////////////////////////////////
// Cubemap capture bake
///////////////////////////////////

void ProbeManager::BeginCaptureBake(const Vec3f& position)
{
	mProbePositions[0] = position;
	mNumProbesPending = 1;
	mCurrentProbe = 0;
	mbCapturePending = true;
	mbCaptureReady = false;

	LogDebug("Probe capture bake armed at {}", position);
}

void ProbeManager::BeginGridBake()
{
	if (mbCapturePending || mbCaptureReady) {
		LogWarning("Probe bake already in progress, ignoring grid bake request");
		return;
	}

	if (!ComputeGridPlacement()) {
		LogError("Probe grid bake failed: could not fit a volume to the level");
		return;
	}

	mNumProbesPending = Limits::MaxIrradianceProbes;
	mCurrentProbe = 0;
	mbCapturePending = true;
	mbCaptureReady = false;

	LogInfo("Probe grid bake armed: {} probes, one per frame", mNumProbesPending);
}

bool ProbeManager::GatherPlacementBoxes(ProbeBoxList& out)
{
	out.Count = 0;
	out.Min = Vec3f(std::numeric_limits<float32>::max());
	out.Max = Vec3f(-std::numeric_limits<float32>::max());
	out.Any = false;

	uint32 stat_total = 0;
	uint32 stat_null = 0;
	uint32 stat_no_mesh = 0;
	uint32 stat_too_big = 0;

	// Fit the volume to reasonably-sized meshed geometry. Absurdly large
	// bounds (sky spheres) are ignored so they can't blow up the volume.
	for (const ObjectID& id : gWorld->GetAllObjects()) {
		stat_total++;

		Object* object = gObjectManager->GetObject(id);
		if (object == nullptr) {
			stat_null++;
			continue;
		}

		if (!object->pMesh.IsValid()) {
			stat_no_mesh++;
			continue;
		}

		const Vec3f bmin = object->GetPosition() + object->Bounds.Min;
		const Vec3f bmax = object->GetPosition() + object->Bounds.Max;

		if ((bmax - bmin).Length() > 100.0f) {
			stat_too_big++;
			continue;
		}

		if (out.Count < ProbeBoxList::scMaxBoxes) {
			out.Boxes[out.Count++] = { bmin, bmax };
		}

		out.Min.X = fminf(out.Min.X, bmin.X);
		out.Min.Y = fminf(out.Min.Y, bmin.Y);
		out.Min.Z = fminf(out.Min.Z, bmin.Z);
		out.Max.X = fmaxf(out.Max.X, bmax.X);
		out.Max.Y = fmaxf(out.Max.Y, bmax.Y);
		out.Max.Z = fmaxf(out.Max.Z, bmax.Z);
		out.Any = true;
	}

	LogInfo("Probe placement: {}/{}/{}/{} total/null/no-mesh/too-big ({} boxes kept)", stat_total, stat_null,
			stat_no_mesh, stat_too_big, out.Count);

	return out.Any;
}

void ProbeManager::PlaceGridProbes(const Vec3f& gmin, const Vec3f& size, const ProbeBoxList& boxes)
{
	const uint32 x_dim = Limits::ProbeGridDims[0];
	const uint32 y_dim = Limits::ProbeGridDims[1];
	const uint32 z_dim = Limits::ProbeGridDims[2];

	uint32 probe_index = 0;

	Vec3f dim_vec = Vec3f(static_cast<float32>(x_dim - 1), static_cast<float32>(y_dim - 1),
						  static_cast<float32>(z_dim - 1));

	for (uint32 iz = 0; iz < z_dim; iz++) {
		for (uint32 iy = 0; iy < y_dim; iy++) {
			for (uint32 ix = 0; ix < x_dim; ix++) {
				Vec3f per_vec = Vec3f(static_cast<float32>(ix), static_cast<float32>(iy), static_cast<float32>(iz));

				// Vec3f p = gmin + Vec3f(size.X * (static_cast<float32>(ix) / static_cast<float32>(x_dim - 1)),
				// 					   size.Y * (static_cast<float32>(iy) / static_cast<float32>(y_dim - 1)),
				// 					   size.Z * (static_cast<float32>(iz) / static_cast<float32>(z_dim - 1)));


				Vec3f p = gmin + (Vec3f(size.X, size.Y, size.Z) * (per_vec / dim_vec));

				// Pull probes out of solid geometry: if inside a box, lift
				// above its top (+0.3m). A few iterations handle stacked boxes.
				for (uint32 iter = 0; iter < 4; iter++) {
					bool inside_any = false;
					for (uint32 b = 0; b < boxes.Count; b++) {
						const float32 margin = 0.1f;
						if (p.X > boxes.Boxes[b].Min.X - margin && p.X < boxes.Boxes[b].Max.X + margin &&
							p.Y > boxes.Boxes[b].Min.Y - margin && p.Y < boxes.Boxes[b].Max.Y + margin &&
							p.Z > boxes.Boxes[b].Min.Z - margin && p.Z < boxes.Boxes[b].Max.Z + margin) {
							p.Y = boxes.Boxes[b].Max.Y + 0.3f;
							inside_any = true;
							break;
						}
					}
					if (!inside_any) {
						break;
					}
				}

				mProbePositions[probe_index++] = p;
			}
		}
	}

	mVolume.Min[0] = gmin.X;
	mVolume.Min[1] = gmin.Y;
	mVolume.Min[2] = gmin.Z;
	mVolume.Min[3] = 0.0f;

	mVolume.InvCellSize[0] = (size.X > 1e-4f) ? (static_cast<float32>(x_dim - 1) / size.X) : 1.0f;
	mVolume.InvCellSize[1] = (size.Y > 1e-4f) ? (static_cast<float32>(y_dim - 1) / size.Y) : 1.0f;
	mVolume.InvCellSize[2] = (size.Z > 1e-4f) ? (static_cast<float32>(z_dim - 1) / size.Z) : 1.0f;
	mVolume.InvCellSize[3] = 0.0f;

	mVolume.DimsAndCount[0] = x_dim;
	mVolume.DimsAndCount[1] = y_dim;
	mVolume.DimsAndCount[2] = z_dim;
	mVolume.DimsAndCount[3] = Limits::MaxIrradianceProbes;

	UploadVolumeToGpu();

	LogInfo("Probe volume: min={} size={} ({} probes)", gmin, size, probe_index);
}

bool ProbeManager::ComputeGridPlacement()
{
	ProbeBoxList boxes {};

	if (!GatherPlacementBoxes(boxes)) {
		LogError("Probe grid bake failed: could not fit a volume to the level");
		return false;
	}

	// Tight padding: the push-out pass handles probes inside geometry.
	const Vec3f gmin = boxes.Min - Vec3f(0.5f, 0.5f, 0.5f);
	const Vec3f gmax = boxes.Max + Vec3f(0.5f, 0.5f, 0.5f);

	PlaceGridProbes(gmin, gmax - gmin, boxes);
	return true;
}

void ProbeManager::BeginGridBakeAt(const Vec3f& center, const Vec3f& size)
{
	if (mbCapturePending || mbCaptureReady) {
		LogWarning("Probe bake already in progress, ignoring grid bake request");
		return;
	}

	ProbeBoxList boxes {};
	GatherPlacementBoxes(boxes); // Only for push-out; bounds come from the caller.

	PlaceGridProbes(center - size * 0.5f, size, boxes);

	mNumProbesPending = Limits::MaxIrradianceProbes;
	mCurrentProbe = 0;
	mbCapturePending = true;
	mbCaptureReady = false;

	LogInfo("Probe grid bake armed at {} over {} ({} probes, one per frame)", center, size, mNumProbesPending);
}

void ProbeManager::EnsureCaptureStage()
{
	if (mbCaptureBuilt) {
		return;
	}

	const Vec2u size(scCaptureSize, scCaptureSize);

	mCaptureStage.Create("ProbeCapture", size, eSizeDivisor::FullRes);

	mCaptureStage.AddTarget(eImageFormat::RGBA16_Float,
							VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
								VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
							eImageAspectFlag::Color);

	mCaptureStage.AddTarget(eImageFormat::D32_Float,
							VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
							eImageAspectFlag::Depth);

	mCaptureStage.BuildRenderStage();

	const uint64 staging_size = static_cast<uint64>(scCaptureSize) * scCaptureSize * sizeof(uint16) * 4;

	for (uint32 i = 0; i < scCaptureFaces; i++) {
		mCaptureStaging[i].Create(renderer::eGpuBufferType::Transfer, staging_size, VMA_MEMORY_USAGE_GPU_TO_CPU,
								  eGpuBufferFlags::TransferReceiver);
	}

	mbCaptureBuilt = true;
}

void ProbeManager::CopyCaptureFaceToStaging(renderer::CommandBuffer& cmd, uint32 face)
{
	Assert(face < scCaptureFaces);

	renderer::Target* target = mCaptureStage.GetTarget(eImageFormat::RGBA16_Float);
	Assert(target != nullptr);

	Image& image = target->Image;

	// The render pass end transitioned the image to SHADER_READ_ONLY; move it
	// to TRANSFER_SRC for the copy. Mirrors Image::SaveToFile barriers.
	VkImageMemoryBarrier pre_barrier {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image.InternalImage,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
						 nullptr, 0, nullptr, 1, &pre_barrier);

	VkBufferImageCopy copy {
		.bufferOffset = 0,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
		.imageExtent = VkExtent3D { .width = scCaptureSize, .height = scCaptureSize, .depth = 1 },
	};

	vkCmdCopyImageToBuffer(cmd, image.InternalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mCaptureStaging[face].Buffer,
						   1, &copy);

	VkImageMemoryBarrier post_barrier {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image.InternalImage,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
						 nullptr, 1, &post_barrier);

	image.ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

static float32 HalfToFloat(uint16 h)
{
	const uint32 sign = (static_cast<uint32>(h & 0x8000) << 16);
	const uint32 exp = (h >> 10) & 0x1F;
	const uint32 mant = h & 0x3FF;

	uint32 f = sign;

	if (exp == 0) {
		// Subnormal or zero: flush to signed zero (negligible for probes).
	}
	else if (exp == 31) {
		f |= 0x7F800000; // inf/nan -> inf (clamped by the caller)
	}
	else {
		f |= ((exp + 112) << 23) | (mant << 13);
	}

	float32 out = 0.0f;
	memcpy(&out, &f, sizeof(out));
	return out;
}

bool ProbeManager::FinishCaptureBake()
{
	if (!mbCaptureReady) {
		return false;
	}

	mbCaptureReady = false;

	if (!mbCaptureBuilt) {
		return false;
	}

	renderer::gGraphics->GetDevice()->WaitForIdle();

	static constexpr uint32 scPixels = scCaptureSize * scCaptureSize;
	static constexpr float32 scRadianceClamp = 16.0f;

	float32 sh[Limits::ProbeSHCoeffCount][3] = {};

	const float32 texel_area = 4.0f / static_cast<float32>(scPixels);

	// LDR copy of what the probe saw (diagnostic dump after the loop).
	SizedArray<uint8> ppm;
	ppm.InitSize(scCaptureFaces * scPixels * 3);

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		mCaptureStaging[face].Map();
		const uint16* pixels = static_cast<const uint16*>(mCaptureStaging[face].pMappedBuffer);

		if (pixels == nullptr) {
			mCaptureStaging[face].UnMap();
			LogError("Probe capture bake failed: could not map staging buffer for face {}", face);
			return false;
		}

		PerspectiveCamera& cam = mFaceCameras[face];


		// MultiplyVec4f is non-const, so work on local copies.
		Mat4f inv_proj = cam.InvProjectionMatrix;
		Mat4f inv_view = cam.InvViewMatrix;

		for (uint32 y = 0; y < scCaptureSize; y++) {
			for (uint32 x = 0; x < scCaptureSize; x++) {
				const uint16* texel = pixels + (y * scCaptureSize + x) * 4;

				const float32 r = fminf(HalfToFloat(texel[0]), scRadianceClamp);
				const float32 g = fminf(HalfToFloat(texel[1]), scRadianceClamp);
				const float32 b = fminf(HalfToFloat(texel[2]), scRadianceClamp);

				uint8* ppm_px = ppm.pData + (face * scPixels + y * scCaptureSize + x) * 3;
				ppm_px[0] = static_cast<uint8>(powf(fminf(r * 0.5f, 1.0f), 1.0f / 2.2f) * 255.0f);
				ppm_px[1] = static_cast<uint8>(powf(fminf(g * 0.5f, 1.0f), 1.0f / 2.2f) * 255.0f);
				ppm_px[2] = static_cast<uint8>(powf(fminf(b * 0.5f, 1.0f), 1.0f / 2.2f) * 255.0f);

				// NDC of the texel center. The sign convention of v does not
				// matter for the weight (even function) and the direction is
				// unprojected through the face camera's own inverse matrices,
				// so this stays consistent with any projection convention.
				const float32 nx = ((static_cast<float32>(x) + 0.5f) / static_cast<float32>(scCaptureSize)) * 2.0f -
								   1.0f;
				const float32 ny = ((static_cast<float32>(y) + 0.5f) / static_cast<float32>(scCaptureSize)) * 2.0f -
								   1.0f;

				Vec4f clip(nx, ny, 0.5f, 1.0f);
				Vec4f view = inv_proj.MultiplyVec4f(clip);
				Vec4f world = inv_view.MultiplyVec4f(view);

				if (fabsf(world.W) < 1e-6f) {
					continue;
				}

				const float32 inv_w = 1.0f / world.W;
				const float32 dx = world.X * inv_w - cam.Position.X;
				const float32 dy = world.Y * inv_w - cam.Position.Y;
				const float32 dz = world.Z * inv_w - cam.Position.Z;

				const float32 len_sq = dx * dx + dy * dy + dz * dz;
				if (len_sq < 1e-12f) {
					continue;
				}

				const float32 inv_len = 1.0f / sqrtf(len_sq);

				// Analytic cube-face solid angle for a 90-degree face.
				const float32 rr = 1.0f + nx * nx + ny * ny;
				const float32 weight = texel_area / (rr * sqrtf(rr));

				float32 basis[Limits::ProbeSHCoeffCount];
				ProbeBasisSH(dx * inv_len, dy * inv_len, dz * inv_len, basis);

				const float32 rgb[3] = { r, g, b };
				for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
					for (uint32 c = 0; c < 3; c++) {
						sh[k][c] += rgb[c] * basis[k] * weight;
					}
				}
			}
		}

		mCaptureStaging[face].UnMap();
	}

	// Debug dump of what the probe saw, so a bad capture can't hide behind SH.
	// Single bakes only: a 64-probe grid would spam hundreds of files.
	if (mNumProbesPending <= 1) {
		static uint32 sBakeIndex = 0;

		char header[32];
		snprintf(header, sizeof(header), "P6\n%u %u\n255\n", scCaptureSize, scCaptureSize);

		for (uint32 f = 0; f < scCaptureFaces; f++) {
			char path[64];
			snprintf(path, sizeof(path), "bakes/probe_bake%u_face%u.ppm", sBakeIndex, f);

			File file(path, File::eModType::Write, File::eDataType::Binary);
			file.Write(header);
			file.WriteRaw(ppm.pData + f * scPixels * 3, scPixels * 3);
			file.Close();
		}

		LogInfo("Probe capture faces dumped as probe_bake{}_faceN.ppm", sBakeIndex);
		sBakeIndex++;
	}
	// Single bakes refresh the whole field (global ambient); grid bakes write
	// only the current cell.
	if (mNumProbesPending <= 1) {
		for (uint32 i = 0; i < Limits::MaxIrradianceProbes; i++) {
			for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
				for (uint32 c = 0; c < 3; c++) {
					mProbes[i].SH[k][c] = sh[k][c];
				}
				mProbes[i].SH[k][3] = 0.0f;
			}
		}
	}
	else {
		ProbeData& probe = mProbes[mCurrentProbe];
		for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
			for (uint32 c = 0; c < 3; c++) {
				probe.SH[k][c] = sh[k][c];
			}
			probe.SH[k][3] = 0.0f;
		}
	}

	UploadToGpu();

	// Mean irradiance (L00 scaled back through the basis) + directional energy,
	// so bakes can be compared from the log without eyeballing pixels.
	const float32 mean_rgb[3] = { sh[0][0] * 0.282095f, sh[0][1] * 0.282095f, sh[0][2] * 0.282095f };
	float32 dir_energy = 0.0f;
	for (uint32 k = 1; k < Limits::ProbeSHCoeffCount; k++) {
		dir_energy += sh[k][0] * sh[k][0] + sh[k][1] * sh[k][1] + sh[k][2] * sh[k][2];
	}

	LogInfo("Probe capture bake finished (probe {}/{}, {} faces) mean=({:.3f}, {:.3f}, {:.3f}) dirE={:.4f}",
			mCurrentProbe + 1, mNumProbesPending, scCaptureFaces, mean_rgb[0], mean_rgb[1], mean_rgb[2],
			sqrtf(dir_energy));
	return true;
}

bool ProbeManager::ServiceCaptureBake()
{
	if (!mbCaptureReady) {
		return mbCapturePending;
	}

	if (!FinishCaptureBake()) {
		mbCapturePending = false;
		mNumProbesPending = 0;
		return false;
	}

	mCurrentProbe++;

	if (mCurrentProbe < mNumProbesPending) {
		// Arm the next probe for the coming frame.
		mbCapturePending = true;

		if ((mCurrentProbe % 8) == 0) {
			LogInfo("Probe grid bake progress: {}/{}", mCurrentProbe, mNumProbesPending);
		}

		return true;
	}

	mbCapturePending = false;
	mNumProbesPending = 0;
	LogInfo("Probe grid bake complete ({} probes)", Limits::MaxIrradianceProbes);
	return false;
}

void ProbeManager::UploadVolumeToGpu()
{
	if (renderer::gGraphics == nullptr || renderer::gGraphics->ProbeVolumeBuffer.Buffer == nullptr) {
		return;
	}

	uint8* base = static_cast<uint8*>(renderer::gGraphics->ProbeVolumeBuffer.pMappedBuffer);
	if (base == nullptr) {
		return;
	}

	const uint32 page_size = renderer::gGraphics->ProbeVolumePageSize;

	for (uint32 frame = 0; frame < renderer::FramesInFlight; frame++) {
		memcpy(base + page_size * frame, &mVolume, sizeof(mVolume));
	}

	renderer::gGraphics->ProbeVolumeBuffer.FlushToGpu(0, page_size * renderer::FramesInFlight);
}

///////////////////////////////////
// Persistence (.fxprobe)
///////////////////////////////////

/// File layout (little-endian, same-machine tool data):
///   char[4]  magic 'FXPR' | uint32 version | uint32 probe_count |
///   ProbeVolumeData volume | ProbeData[probe_count]
struct FxProbeHeader
{
	char Magic[4] = { 'F', 'X', 'P', 'R' };
	uint32 Version = 1;
	uint32 ProbeCount = Limits::MaxIrradianceProbes;
};

String ProbeManager::GetProbeFilePath() const
{
	return String::Fmt("{}/probes.fxprobe", gAssetManager->GetScenePath().CStr());
}

bool ProbeManager::SaveProbes()
{
	const String path = GetProbeFilePath();

	FxProbeHeader header {};

	File file(path, File::eModType::Write, File::eDataType::Binary);
	file.WriteRaw(&header, sizeof(header));
	file.WriteRaw(&mVolume, sizeof(mVolume));
	file.WriteRaw(mProbes, sizeof(mProbes));
	file.Close();

	LogInfo("Saved {} light probes to {}", Limits::MaxIrradianceProbes, path.CStr());
	return true;
}

bool ProbeManager::LoadProbes()
{
	const String path = GetProbeFilePath();

	if (!std::filesystem::exists(path.CStr())) {
		LogInfo("No probe file at {}, using procedural probes", path.CStr());
		return false;
	}

	File file(path, File::eModType::Read, File::eDataType::Binary);
	Slice<uint8> data = file.Read<uint8>();
	file.Close();

	const uint64 expected_size = sizeof(FxProbeHeader) + sizeof(ProbeVolumeData) + sizeof(mProbes);

	if (data.Size != expected_size) {
		LogError("Probe file {} has wrong size ({} != {}), ignoring", path.CStr(), data.Size, expected_size);
		return false;
	}

	const FxProbeHeader* header = reinterpret_cast<const FxProbeHeader*>(data.pData);

	if (header->Magic[0] != 'F' || header->Magic[1] != 'X' || header->Magic[2] != 'P' || header->Magic[3] != 'R') {
		LogError("Probe file {} has bad magic, ignoring", path.CStr());
		return false;
	}

	if (header->Version != 1 || header->ProbeCount != Limits::MaxIrradianceProbes) {
		LogError("Probe file {} is version {} with {} probes (expected v1 x{}), ignoring", path.CStr(), header->Version,
				 header->ProbeCount, Limits::MaxIrradianceProbes);
		return false;
	}

	memcpy(&mVolume, data.pData + sizeof(FxProbeHeader), sizeof(mVolume));
	memcpy(mProbes, data.pData + sizeof(FxProbeHeader) + sizeof(mVolume), sizeof(mProbes));

	UploadVolumeToGpu();
	UploadToGpu();

	LogInfo("Loaded {} light probes from {}", Limits::MaxIrradianceProbes, path.CStr());
	return true;
}

void ProbeManager::UploadToGpu()
{
	if (renderer::gGraphics == nullptr || renderer::gGraphics->ProbeBuffer.Buffer == nullptr) {
		return;
	}

	uint8* base = static_cast<uint8*>(renderer::gGraphics->ProbeBuffer.pMappedBuffer);
	if (base == nullptr) {
		return;
	}

	const uint32 page_size = renderer::gGraphics->ProbePageSize;
	const uint32 data_size = sizeof(ProbeData) * Limits::MaxIrradianceProbes;

	for (uint32 frame = 0; frame < renderer::FramesInFlight; frame++) {
		memcpy(base + page_size * frame, mProbes, data_size);
	}

	renderer::gGraphics->ProbeBuffer.FlushToGpu(0, page_size * renderer::FramesInFlight);
}

} // namespace fx
