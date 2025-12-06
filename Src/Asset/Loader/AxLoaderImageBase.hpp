#pragma once

#include "AxLoaderBase.hpp"

#include <Renderer/Backend/RxImage.hpp>

class AxLoaderImageBase : public AxLoaderBase
{
public:
    using Status = AxLoaderBase::Status;

public:
    RxImageType ImageType;
    VkFormat ImageFormat = VK_FORMAT_UNDEFINED;
    int16 ImageNumComponents = 0;
};
