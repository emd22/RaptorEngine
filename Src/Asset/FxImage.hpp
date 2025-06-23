#pragma once

#include "FxBaseAsset.hpp"

#include <Renderer/Backend/RvkTexture.hpp>

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
            return;
        }

        Texture.Destroy();

        // mImageReady = false;
        IsUploadedToGpu = false;
    }

public:
    RvkTexture Texture;

    uint32 NumComponents = 3;
    Vec2u Size = Vec2u::Zero;

private:
    // bool mImageReady = false;
};
