#include "AxImage.hpp"

#include <Core/FxPagedArray.hpp>
#include <Core/FxRef.hpp>


static FxPagedArray<FxRef<AxImage>> mEmptyImagesPerFormat;

FxPagedArray<FxRef<AxImage>>& AxImage::GetEmptyImagesArray() { return mEmptyImagesPerFormat; }

void AxImage::Destroy()
{
    if (!bIsUploadedToGpu.load()) {
        return;
    }

    Texture.Destroy();

    // mImageReady = false;
    bIsUploadedToGpu = false;
}

void AxImage::MarkAndSignalLoaded()
{
    IsFinishedNotifier.SignalDataWritten();

    bIsUploadedToGpu = true;
    bIsUploadedToGpu.notify_all();

    mIsLoaded.store(true);
}
