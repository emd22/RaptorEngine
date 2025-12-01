#pragma once

#include "FxLoaderBase.hpp"

#include <Renderer/Backend/RxImage.hpp>

class FxLoaderImageBase : public FxLoaderBase
{
public:
    using Status = FxLoaderBase::Status;

public:
    RxImageType ImageType;
    VkFormat ImageFormat = VK_FORMAT_UNDEFINED;
    int16 ImageNumComponents = 0;
};
