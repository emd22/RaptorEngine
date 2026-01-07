#pragma once

#include "AxBase.hpp"

#include <Core/FxRef.hpp>
#include <Renderer/Backend/RxTexture.hpp>

// struct FxAssetImageTransform
// {
//     enum class Type
//     {
//         eNone,
//         eTruncatePixels,
//     };

// public:

//     Type TransformType = Type::eNone;
// };


class AxImage : public AxBase
{
protected:
public:
    AxImage() {}

    friend class AxManager;


public:
    // static FxRef<FxAssetImage> GetEmptyImage(VkFormat format, int32 num_channels);

    static FxPagedArray<FxRef<AxImage>>& GetEmptyImagesArray();

    template <VkFormat TFormat, int32 TNumChannels>
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

        FxSizedArray<uint8> image_data(TNumChannels);
        memset(image_data.pData, 1, TNumChannels);
        image_data.MarkFull();

        sEmptyImage = FxMakeRef<AxImage>();

        sEmptyImage->Texture.Create(RxImageType::eImage, image_data, FxVec2u(1, 1), TFormat, TNumChannels);
        sEmptyImage->IsFinishedNotifier.SignalDataWritten();
        sEmptyImage->bIsUploadedToGpu = true;
        sEmptyImage->bIsUploadedToGpu.notify_all();
        sEmptyImage->mIsLoaded.store(true);

        empty_images.Insert(sEmptyImage);

        return sEmptyImage;
    }


    ~AxImage() override { Destroy(); }

    // private:
    void Destroy() override;

public:
    RxTexture Texture;

    uint32 NumComponents = 3;

    RxImageType ImageType = RxImageType::eImage;
    FxVec2u Size = FxVec2u::sZero;

private:
    // bool mImageReady = false;
};
