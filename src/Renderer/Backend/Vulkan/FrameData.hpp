#pragma once

#include "Semaphore.hpp"
#include "Fence.hpp"

#include "FxCommandPool.hpp"
#include "FxCommandBuffer.hpp"

namespace vulkan {

class FrameData
{
public:
    void Create(GPUDevice *device);
    void Destroy();
public:
    FxCommandPool CommandPool;
    FxCommandBuffer CommandBuffer;

    Semaphore ImageAvailable;
    Semaphore RenderFinished;
    Fence InFlight;
};

};
