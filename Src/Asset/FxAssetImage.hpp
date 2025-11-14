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
    static FxRef<FxAssetImage> GetEmptyImage();

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
