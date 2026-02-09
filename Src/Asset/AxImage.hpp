#pragma once

#include "AxBase.hpp"

#include <Core/FxRef.hpp>
#include <Renderer/Backend/RxImage.hpp>

class AxImage : public AxBase
{
protected:
public:
    AxImage() {}

    friend class AxManager;


public:
    // static FxRef<FxAssetImage> GetEmptyImage(VkFormat format, int32 num_channels);

    static FxPagedArray<FxRef<AxImage>>& GetEmptyImagesArray();

    template <RxImageFormat TFormat>
    static FxRef<AxImage> GetEmptyImage()
    {
        static FxRef<AxImage> sEmptyImage { nullptr };

        if (sEmptyImage) {
            return sEmptyImage;
        }

        FxPagedArray<FxRef<AxImage>>& empty_images = GetEmptyImagesArray();

        if (!empty_images.IsInited()) {
            empty_images.Create(8);
        }

        constexpr uint32 pixel_size = RxImageFormatUtil::GetSize(TFormat);

        FxSizedArray<uint8> image_data(pixel_size);
        memset(image_data.pData, 1, pixel_size);
        image_data.MarkFull();

        sEmptyImage = FxMakeRef<AxImage>();

        sEmptyImage->Image.CreateGpuOnly(RxImageType::e2d, FxVec2u(1, 1), TFormat, image_data);
        sEmptyImage->MarkAndSignalLoaded();

        empty_images.Insert(sEmptyImage);

        return sEmptyImage;
    }

    /**
     * Signal to any assets that are waiting that this asset is now loaded and ready to be used.
     */
    void MarkAndSignalLoaded();

    ~AxImage() override { Destroy(); }

    // private:
    void Destroy() override;

public:
    RxImage Image;

    RxImageType ImageType = RxImageType::e2d;
    FxVec2u Size = FxVec2u::sZero;
};
