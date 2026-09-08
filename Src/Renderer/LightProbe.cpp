/*
 * File:        LightProbe.cpp
 * Description: MVP light probe manager (SH L2 diffuse irradiance).
 */

#include "LightProbe.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

#include <cmath>
#include <cstring>

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

	// Default reproduces the old flat ambient (10,10,10 sRGB-ish) with a faint
	// cool sky / dark ground gradient so probe evaluation is visibly directional.
	const float32 sky[3] = { 0.055f, 0.062f, 0.075f };
	const float32 ground[3] = { 0.025f, 0.022f, 0.020f };
	mProbes[0] = MakeSkyGradientProbe(sky, ground);
	mProbeCount = 1;

	UploadToGpu();

	mbInitialized = true;
}

void ProbeManager::Destroy()
{
	mbInitialized = false;
	mbCapturePending = false;
	mbCaptureReady = false;

	for (uint32 i = 0; i < scCaptureFaces; i++) {
		mCaptureStaging[i].Destroy();
	}
}

void ProbeManager::SetUniformAmbient(float32 r, float32 g, float32 b)
{
	mProbes[0] = MakeUniformAmbientProbe(r, g, b);
	mProbeCount = 1;
	UploadToGpu();
}

void ProbeManager::SetSkyGradient(const float32 sky[3], const float32 ground[3])
{
	mProbes[0] = MakeSkyGradientProbe(sky, ground);
	mProbeCount = 1;
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

	ProbeData& probe = mProbes[0];
	for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
		for (uint32 c = 0; c < 3; c++) {
			probe.SH[k][c] = sh[k][c] * scFourPiOverN;
		}
		probe.SH[k][3] = 0.0f;
	}

	mProbeCount = 1;
	UploadToGpu();
}

///////////////////////////////////
// Cubemap capture bake
///////////////////////////////////

void ProbeManager::BeginCaptureBake(const Vec3f& position)
{
	mCapturePosition = position;
	mbCapturePending = true;
	mbCaptureReady = false;

	LogDebug("Probe capture bake armed at {}", position);
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

	vkCmdCopyImageToBuffer(cmd, image.InternalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						   mCaptureStaging[face].Buffer, 1, &copy);

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

	for (uint32 face = 0; face < scCaptureFaces; face++) {
		mCaptureStaging[face].Map();
		const uint16* pixels = static_cast<const uint16*>(mCaptureStaging[face].pMappedBuffer);

		if (pixels == nullptr) {
			mCaptureStaging[face].UnMap();
			LogError("Probe capture bake failed: could not map staging buffer for face {}", face);
			return false;
		}

		const PerspectiveCamera& cam = mFaceCameras[face];

		// MultiplyVec4f is non-const, so work on local copies.
		Mat4f inv_proj = cam.InvProjectionMatrix;
		Mat4f inv_view = cam.InvViewMatrix;

		for (uint32 y = 0; y < scCaptureSize; y++) {
			for (uint32 x = 0; x < scCaptureSize; x++) {
				const uint16* texel = pixels + (y * scCaptureSize + x) * 4;

				const float32 r = fminf(HalfToFloat(texel[0]), scRadianceClamp);
				const float32 g = fminf(HalfToFloat(texel[1]), scRadianceClamp);
				const float32 b = fminf(HalfToFloat(texel[2]), scRadianceClamp);

				// NDC of the texel center. The sign convention of v does not
				// matter for the weight (even function) and the direction is
				// unprojected through the face camera's own inverse matrices,
				// so this stays consistent with any projection convention.
				const float32 nx =
					((static_cast<float32>(x) + 0.5f) / static_cast<float32>(scCaptureSize)) * 2.0f - 1.0f;
				const float32 ny =
					((static_cast<float32>(y) + 0.5f) / static_cast<float32>(scCaptureSize)) * 2.0f - 1.0f;

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

	ProbeData& probe = mProbes[0];
	for (uint32 k = 0; k < Limits::ProbeSHCoeffCount; k++) {
		for (uint32 c = 0; c < 3; c++) {
			probe.SH[k][c] = sh[k][c];
		}
		probe.SH[k][3] = 0.0f;
	}

	mProbeCount = 1;
	UploadToGpu();

	LogInfo("Probe capture bake finished ({} faces, {}px)", scCaptureFaces, scCaptureSize);
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
