#pragma once

#include "AxLoaderBase.hpp"

#include <Renderer/Backend/RxImage.hpp>

namespace fx {


class AxLoaderImageBase : public AxLoaderBase
{
public:
    using Status = AxLoaderBase::Status;

public:
    renderer::RxImageType ImageType = renderer::RxImageType::e2d;
    renderer::RxImageFormat ImageFormat = renderer::RxImageFormat::eNone;
    // int16 ImageNumComponents = 0;
};

} // namespace fx
