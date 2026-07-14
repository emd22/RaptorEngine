#pragma once

#include "AssetBase.hpp"

#include <Core/PagedArray.hpp>
#include <Core/Ref.hpp>
#include <Renderer/Backend/Image.hpp>
#include <Renderer/Backend/RenderBackendFwd.hpp>

namespace fx {

class AxImage : public AssetBase
{
protected:
public:
    friend class AssetManager;

    AxImage() = default;
    AxImage(const AxImage& other);

    static PagedArray<AxImage>& GetEmptyImagesArray();

    template <eImageFormat TFormat>
    static TSRef<AxImage> GetEmptyImage()
    {
        // Stashed ptr for the image
        static AxImage* spEmptyImage { nullptr };

        if (spEmptyImage) {
            return spEmptyImage;
        }

        PagedArray<AxImage>& empty_images = GetEmptyImagesArray();

        if (!empty_images.IsInited()) {
            empty_images.Create(10);
        }

        constexpr uint32 pixel_size = renderer::ImageFormatUtil::GetPixelStride(TFormat);

        SizedArray<uint8> image_data(pixel_size);
        memset(image_data.pData, 1, pixel_size);
        image_data.MarkFull();

        spEmptyImage = empty_images.Insert();

        renderer::RenderBackendFwd::SubmitImmediateUploadCmd(
            [&](renderer::CommandBuffer& cmd)
            {
                ImageInfo image_info { Vec2u(1, 1), TFormat, 0, 1,
                                       Slice<const uint8>(image_data.pData, image_data.Size) };

                spEmptyImage->Image.CreateFromData(cmd, image_info, eImageCreateFlags::None);
                spEmptyImage->MarkAndSignalLoaded();
            });

        return spEmptyImage;
    }

    AxImage& operator=(const AxImage& other);

    /**
     * Signal to any assets that are waiting that this asset is now loaded and ready to be used.
     */
    void MarkAndSignalLoaded();

    ~AxImage() override { Destroy(); }

    void Destroy() override;

public:
    renderer::Image Image {};

    eImageType ImageType = eImageType::Flat;
};

} // namespace fx
