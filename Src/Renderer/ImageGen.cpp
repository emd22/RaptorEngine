#include "ImageGen.hpp"

#include "Backend/Image.hpp"
#include "Globals.hpp"
#include "GraphicsBackend.hpp"

#include <Asset/Loader/Image/LoaderStb.hpp>
#include <Core/Random.hpp>
#include <Math/SIMDHelper.hpp>
#include <Texture/TextureManager.hpp>
#include <Util/RustInterop.hpp>
#include <algorithm>
#include <cmath>

namespace fx::renderer {

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

	const int32 generated = rx_dfg_lut(size, pixel_data.pData);
	Assert(generated != 0);

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
