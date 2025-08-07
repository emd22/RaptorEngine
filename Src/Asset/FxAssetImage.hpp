#pragma once

#include "FxAssetBase.hpp"

#include <Renderer/Backend/RxTexture.hpp>

#include <Core/FxStaticPtrArray.hpp>


class FxAssetImage : public FxAssetBase
{
protected:
public:
    FxAssetImage()
    {
    }

    friend class FxAssetManager;

public:
    ~FxAssetImage() override
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
    RxTexture Texture;

    uint32 NumComponents = 3;
    FxVec2u Size = FxVec2u::Zero;

private:
    // bool mImageReady = false;
};
