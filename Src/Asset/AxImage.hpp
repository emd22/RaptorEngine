#pragma once

#include "AxBase.hpp"

#include <Core/Ref.hpp>
#include <Renderer/Backend/RxImage.hpp>

namespace fx {

class AxImage : public AxBase
{
protected:
public:
    friend class AxManager;

    AxImage() = default;
    AxImage(const AxImage& other);

    static PagedArray<TSRef<AxImage>>& GetEmptyImagesArray();

    template <renderer::RxImageFormat TFormat>
    static TSRef<AxImage> GetEmptyImage()
    {
        // Stashed ptr for the image
        static TSRef<AxImage> spEmptyImage { nullptr };

        if (spEmptyImage) {
            return spEmptyImage;
        }

        PagedArray<TSRef<AxImage>>& empty_images = GetEmptyImagesArray();

        if (!empty_images.IsInited()) {
            empty_images.Create(10);
        }

        constexpr uint32 pixel_size = renderer::RxImageFormatUtil::GetSize(TFormat);

        SizedArray<uint8> image_data(pixel_size);
        memset(image_data.pData, 1, pixel_size);
        image_data.MarkFull();

        spEmptyImage = TSRef<AxImage>::New();
        spEmptyImage->Image.CreateGpuOnly(renderer::RxImageType::e2d, Vec2u(1, 1), TFormat, image_data);
        spEmptyImage->MarkAndSignalLoaded();

        empty_images.Insert(spEmptyImage);

        return spEmptyImage;
    }

    AxImage& operator=(const AxImage& other);

    /**
     * Signal to any assets that are waiting that this asset is now loaded and ready to be used.
     */
    void MarkAndSignalLoaded();

    ~AxImage() override { Destroy(); }

    // private:
    void Destroy() override;

public:
    renderer::RxImage Image {};

    renderer::RxImageType ImageType = renderer::RxImageType::e2d;
    Vec2u Size = Vec2u::sZero;
};

} // namespace fx
