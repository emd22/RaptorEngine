#include "ImageGen.hpp"

#include "Backend/Image.hpp"
#include "Globals.hpp"
#include "GraphicsBackend.hpp"

#include <Texture/TextureManager.hpp>

namespace fx {


/////////////////////////////////////
// Image Gen functions
/////////////////////////////////////

namespace ImageGen {

Image* Random(Vec2u size)
{
	Image* image = gTextureManager->NewTexture();

	const uint64 total_image_size = (static_cast<uint64>(size.X) * size.Y * 4ULL);

	SizedArray<uint8> pixel_data;
	pixel_data.InitSize(total_image_size);

	for (uint64 i = 0; i < pixel_data.Size; i += 4ULL) {
	}

	renderer::gGraphics->SubmitImmediateUploadCmd(
		[&](renderer::CommandBuffer& cmd)
		{
			ImageInfo info(size, eImageFormat::RGBA8_UNorm, 0, 1,
						   Slice<const uint8>(pixel_data.pData, pixel_data.Size));

			image->CreateFromData(cmd, info, eImageCreateFlags::None);
		});

	return image;
}

} // namespace ImageGen


} // namespace fx
