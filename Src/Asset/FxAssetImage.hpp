#pragma once

#include "FxAssetBase.hpp"

#include <Core/FxRef.hpp>
#include <Renderer/Backend/RxTexture.hpp>

class FxAssetImage : public FxAssetBase
{
protected:
public:
    FxAssetImage() {}

    friend class FxAssetManager;


public:
    // static FxRef<FxAssetImage> GetEmptyImage(VkFormat format, int32 num_channels);

    template <VkFormat TFormat, int32 TNumChannels>
    static FxRef<FxAssetImage> GetEmptyImage()
    {
        static FxRef<FxAssetImage> sEmptyImage { nullptr };

        if (sEmptyImage) {
            return sEmptyImage;
        }

        FxSizedArray<uint8> image_data(TNumChannels);
        memset(image_data.pData, 1, TNumChannels);
        image_data.MarkFull();

        sEmptyImage = FxMakeRef<FxAssetImage>();

        sEmptyImage->Texture.Create(RxImageType::Image, image_data, FxVec2u(1, 1), TFormat, TNumChannels);
        sEmptyImage->IsFinishedNotifier.SignalDataWritten();
        sEmptyImage->bIsUploadedToGpu = true;
        sEmptyImage->bIsUploadedToGpu.notify_all();
        sEmptyImage->mIsLoaded.store(true);

        return sEmptyImage;
    }


    ~FxAssetImage() override { Destroy(); }

    // private:
    void Destroy() override;

public:
    RxTexture Texture;

    uint32 NumComponents = 3;

    RxImageType ImageType = RxImageType::Image;
    FxVec2u Size = FxVec2u::sZero;

private:
    // bool mImageReady = false;
};
