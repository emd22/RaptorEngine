#include "FxAssetImage.hpp"

#include <Core/FxRef.hpp>

static FxRef<FxAssetImage> sEmptyImage { nullptr };


void FxAssetImage::Destroy()
{
    if (!bIsUploadedToGpu.load()) {
        return;
    }

    Texture.Destroy();

    // mImageReady = false;
    bIsUploadedToGpu = false;
}

FxRef<FxAssetImage> FxAssetImage::GetEmptyImage()
{
    if (sEmptyImage) {
        return sEmptyImage;
    }

    const FxSizedArray<uint8> image_data = { 1, 1, 1, 1 };

    sEmptyImage = FxMakeRef<FxAssetImage>();

    sEmptyImage->Texture.Create(RxImageType::Image, image_data, FxVec2u(1, 1), VK_FORMAT_R8G8B8A8_SRGB, 4);
    sEmptyImage->IsFinishedNotifier.SignalDataWritten();
    sEmptyImage->bIsUploadedToGpu = true;
    sEmptyImage->bIsUploadedToGpu.notify_all();
    sEmptyImage->mIsLoaded.store(true);

    return sEmptyImage;
}
