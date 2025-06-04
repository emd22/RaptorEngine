#pragma once

#include "FxBaseAsset.hpp"

#include <Renderer/FxMesh.hpp>

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


private:
    bool mImageReady = false;
};
