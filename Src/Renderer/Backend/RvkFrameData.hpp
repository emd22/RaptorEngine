#pragma once

#include "RvkSynchro.hpp"

#include "RvkCommands.hpp"
#include "RvkDescriptors.hpp"

#include "RvkGpuBuffer.hpp"
#include <Math/Mat4.hpp>


struct alignas(16) RvkUniformBufferObject
{
    Mat4f MvpMatrix;
};


struct RvkFrameData
{
public:
    void Create(RvkGpuDevice *device);
    void SubmitUbo(const RvkUniformBufferObject& ubo);
    void Destroy();
public:
    RvkCommandPool CommandPool;
    RvkCommandBuffer CommandBuffer;
    RvkCommandBuffer CompCommandBuffer;

    // RvkDescriptorSet DescriptorSet;
    RvkDescriptorSet CompDescriptorSet;

    RvkRawGpuBuffer<RvkUniformBufferObject> Ubo;

    RvkSemaphore OffscreenSem;

    RvkSemaphore ImageAvailable;
    RvkSemaphore RenderFinished;
    RvkFence InFlight;
};
