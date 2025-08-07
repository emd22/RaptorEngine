#pragma once

#include "RxSynchro.hpp"

#include "RxCommands.hpp"
#include "RxDescriptors.hpp"

#include "RxGpuBuffer.hpp"
#include <Math/Mat4.hpp>


struct alignas(16) RxUniformBufferObject
{
    FxMat4f MvpMatrix;
};


struct RxFrameData
{
public:
    void Create(RxGpuDevice *device);
    void SubmitUbo(const RxUniformBufferObject& ubo);
    void Destroy();
public:
    RxCommandPool CommandPool;
    RxCommandBuffer CommandBuffer;
    RxCommandBuffer LightCommandBuffer;
    RxCommandBuffer CompCommandBuffer;

    // RxDescriptorSet DescriptorSet;
    RxDescriptorSet CompDescriptorSet;

    // RxRawGpuBuffer<RxUniformBufferObject> Ubo;

    RxSemaphore OffscreenSem;
    RxSemaphore LightingSem;

    RxSemaphore ImageAvailable;
    RxSemaphore RenderFinished;
    RxFence InFlight;
};
