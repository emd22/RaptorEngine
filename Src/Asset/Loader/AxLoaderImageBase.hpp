#pragma once

#include "AxLoaderBase.hpp"

#include <Renderer/Backend/Image.hpp>

namespace fx {

class AxLoaderImageBase : public AxLoaderBase
{
public:
    using eStatus = fx::AxLoaderBase::eStatus;

public:
    renderer::eImageType ImageType = renderer::eImageType::Flat;
    renderer::eImageFormat ImageFormat = renderer::eImageFormat::None;
    // int16 ImageNumComponents = 0;
    //
    eImageCreateFlags CreationFlags = eImageCreateFlags::None;
};

} // namespace fx
