#pragma once

#include "AxLoaderBase.hpp"

#include <Renderer/Backend/RxImage.hpp>

class AxLoaderImageBase : public AxLoaderBase
{
public:
    using Status = AxLoaderBase::Status;

public:
    RxImageType ImageType;
    RxImageFormat ImageFormat = RxImageFormat::eNone;
    //int16 ImageNumComponents = 0;
};
