#include "FxAssetImage.hpp"

#include <Core/FxRef.hpp>


void FxAssetImage::Destroy()
{
    if (!bIsUploadedToGpu.load()) {
        return;
    }

    Texture.Destroy();

    // mImageReady = false;
    bIsUploadedToGpu = false;
}
