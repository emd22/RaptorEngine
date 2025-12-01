#include "FxAssetImage.hpp"

#include <Core/FxPagedArray.hpp>
#include <Core/FxRef.hpp>


static FxPagedArray<FxRef<FxAssetImage>> mEmptyImagesPerFormat;

FxPagedArray<FxRef<FxAssetImage>>& FxAssetImage::GetEmptyImagesArray() { return mEmptyImagesPerFormat; }

void FxAssetImage::Destroy()
{
    if (!bIsUploadedToGpu.load()) {
        return;
    }

    Texture.Destroy();

    // mImageReady = false;
    bIsUploadedToGpu = false;
}
