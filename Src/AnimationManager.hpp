#pragma once


#include <Core/Types.hpp>
#include <Renderer/Backend/RxGpuBuffer.hpp>

namespace fx {

class RxAnimationManager
{
public:
    RxAnimationManager() = default;

    ~RxAnimationManager() = default;

private:
    renderer::RxRawGpuBuffer PoseBuffer;
};

} // namespace fx
