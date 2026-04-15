#pragma once

#include "RxCommands.hpp"
#include "RxDescriptors.hpp"
#include "RxSynchro.hpp"

#include <Math/Mat4.hpp>

namespace fx::renderer {

struct alignas(16) RxUniformBufferObject
{
    Mat4f MvpMatrix;
};

struct RxFrameData
{
public:
    void Create(RxGpuDevice* device);
    void SubmitUbo(const RxUniformBufferObject& ubo);
    void Destroy();

public:
    RxCommandPool CommandPool;

    RxCommandBuffer CommandBuffer;

    // RxDescriptorSet DescriptorSet;
    RxDescriptorSet CompDescriptorSet;

    // RxRawGpuBuffer<RxUniformBufferObject> Ubo;

    RxSemaphore OffscreenSem;
    RxSemaphore LightingSem;
    RxSemaphore ShadowsSem;

    RxSemaphore ImageAvailable;
    RxSemaphore RenderFinished;
    RxFence InFlight;
};

} // namespace fx::renderer
