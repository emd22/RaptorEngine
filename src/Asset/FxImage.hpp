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

        mImageReady = false;
        IsUploadedToGpu = false;
    }

public:
    vulkan::RvkTexture Texture;

private:
    bool mImageReady = false;
};
