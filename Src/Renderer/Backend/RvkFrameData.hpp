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
    void SubmitUbo(RvkUniformBufferObject *ubo);
    void Destroy();
public:
    RvkCommandPool CommandPool;
    RvkCommandBuffer CommandBuffer;

    RvkDescriptorSet DescriptorSet;

    RvkRawGpuBuffer<RvkUniformBufferObject> UniformBuffer;

    RvkSemaphore ImageAvailable;
    RvkSemaphore RenderFinished;
    RvkFence InFlight;
};
