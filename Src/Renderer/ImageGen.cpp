#include "ImageGen.hpp"

#include "Backend/Image.hpp"
#include "Globals.hpp"
#include "GraphicsBackend.hpp"

#include <Asset/Loader/Image/LoaderStb.hpp>
#include <Core/Random.hpp>
#include <Math/SIMDHelper.hpp>
#include <Texture/TextureManager.hpp>
#include <algorithm>
#include <cmath>

namespace fx::renderer {

namespace {

constexpr uint32 scDfgSampleCount = 1024;
constexpr float64 scDfgMinNdotV = 1e-3;

float64 RadicalInverse(uint32 bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);

	return static_cast<float64>(bits) * 2.3283064365386963e-10;
}

float64 SmithGGXCorrelated(float64 n_dot_l, float64 n_dot_v, float64 alpha)
{
	const float64 alpha_sq = alpha * alpha;

	const float64 lambda_v = n_dot_l * std::sqrt((n_dot_v * n_dot_v * (1.0 - alpha_sq)) + alpha_sq);
	const float64 lambda_l = n_dot_v * std::sqrt((n_dot_l * n_dot_l * (1.0 - alpha_sq)) + alpha_sq);

	return 0.5 / (lambda_v + lambda_l);
}

void IntegrateDfg(float64 n_dot_v, float64 roughness, float64& out_scale, float64& out_bias)
{
	const float64 alpha = roughness * roughness;
	const float64 alpha_sq = alpha * alpha;

	const float64 view_x = std::sqrt(1.0 - (n_dot_v * n_dot_v));
	const float64 view_z = n_dot_v;

	float64 scale = 0.0;
	float64 bias = 0.0;

	for (uint32 i = 0; i < scDfgSampleCount; i++) {
		const float64 u1 = (static_cast<float64>(i) + 0.5) / scDfgSampleCount;
		const float64 u2 = RadicalInverse(i);

		const float64 phi = 2.0 * M_PI * u1;
		const float64 cos_theta = std::sqrt((1.0 - u2) / (1.0 + ((alpha_sq - 1.0) * u2)));
		const float64 sin_theta = std::sqrt(std::max(0.0, 1.0 - (cos_theta * cos_theta)));

		const float64 half_x = sin_theta * std::cos(phi);
		const float64 half_z = cos_theta;

		const float64 v_dot_h = (view_x * half_x) + (view_z * half_z);
		const float64 n_dot_l = (2.0 * v_dot_h * half_z) - view_z;

		if (n_dot_l <= 0.0 || v_dot_h <= 0.0) {
			continue;
		}

		const float64 weight = 4.0 * SmithGGXCorrelated(n_dot_l, n_dot_v, alpha) * v_dot_h * n_dot_l / half_z;
		const float64 fresnel = std::pow(1.0 - v_dot_h, 5.0);

		scale += (1.0 - fresnel) * weight;
		bias += fresnel * weight;
	}

	out_scale = scale / scDfgSampleCount;
	out_bias = bias / scDfgSampleCount;
}

uint16 EncodeUNorm16(float64 value) { return static_cast<uint16>((std::clamp(value, 0.0, 1.0) * 65535.0) + 0.5); }

} // namespace


/////////////////////////////////////
// Image Gen functions
/////////////////////////////////////

namespace ImageGen {

Image* Random(Vec2u size)
{
	Image* image = gTextureManager->NewTexture();

	const uint64 total_image_size = (static_cast<uint64>(size.X) * size.Y * 4ULL);

	SizedArray<uint32> pixel_data;
	pixel_data.InitSize(total_image_size);

	for (uint64 i = 0; i < pixel_data.Size; i += 4ULL) {
		// Generate 4 random values
		UINT4 rv = FastRand4();
		simd::StoreUInt4(pixel_data.pData + i, rv);
	}

	// loader::LoaderStb::SaveToFile(eImageSaveFormat::Jpeg,
	// 							  Slice<const uint8>(reinterpret_cast<const uint8*>(pixel_data.pData), pixel_data.Size),
	// 							  size, "chud.jpeg", eImageSaveFlags::None);

	renderer::gGraphics->SubmitImmediateUploadCmd(
		[&](renderer::CommandBuffer& cmd)
		{
			ImageInfo info(size, eImageFormat::R32_UInt, 0, 1,
						   Slice<const uint8>(reinterpret_cast<const uint8*>(pixel_data.pData), pixel_data.Size));

			image->CreateFromData(cmd, info, eImageCreateFlags::None);
		});

	return image;
}

Image* DfgLut(uint32 size)
{
	Image* image = gTextureManager->NewTexture();

	SizedArray<uint16> pixel_data;
	pixel_data.InitSize(static_cast<uint64>(size) * size * 2ULL);

	const float64 last_texel = static_cast<float64>(size - 1);

	for (uint32 y = 0; y < size; y++) {
		for (uint32 x = 0; x < size; x++) {
			const float64 n_dot_v = std::max(static_cast<float64>(x) / last_texel, scDfgMinNdotV);
			const float64 roughness = static_cast<float64>(y) / last_texel;

			float64 scale = 0.0;
			float64 bias = 0.0;
			IntegrateDfg(n_dot_v, roughness, scale, bias);

			uint16* texel = pixel_data.pData + (((static_cast<uint64>(y) * size) + x) * 2ULL);
			texel[0] = EncodeUNorm16(scale);
			texel[1] = EncodeUNorm16(bias);
		}
	}

	renderer::gGraphics->SubmitImmediateUploadCmd(
		[&](renderer::CommandBuffer& cmd)
		{
			ImageInfo info(Vec2u(size, size), eImageFormat::RG16_UNorm, 0, 1,
						   Slice<const uint8>(reinterpret_cast<const uint8*>(pixel_data.pData),
											  pixel_data.GetSizeInBytes()));

			image->CreateFromData(cmd, info, eImageCreateFlags::None);
		});

	return image;
}

} // namespace ImageGen


} // namespace fx::renderer
