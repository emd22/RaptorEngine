#pragma once

#include "FxBaseAsset.hpp"

#include <Renderer/Backend/Vulkan/RvkTexture.hpp>

#include <Core/FxStaticPtrArray.hpp>


class FxImage : public FxBaseAsset
{
protected:
public:
    FxImage()
    {
    }

    friend class FxAssetManager;

public:
    ~FxImage() override
    {
        Destroy();
    }

// private:
    void Destroy() override
    {
        if (!IsUploadedToGpu.load()) {
            printf("Not uploaded to GPU!\n");
            return;
        }

        Texture.Destroy();

        // mImageReady = false;
        IsUploadedToGpu = false;
    }

public:
    vulkan::RvkTexture Texture;

    uint32 NumComponents = 3;
    Vec2u Size = Vec2u::Zero;

private:
    // bool mImageReady = false;
};
