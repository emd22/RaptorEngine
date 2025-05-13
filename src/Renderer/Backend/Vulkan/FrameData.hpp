#pragma once

#include "Semaphore.hpp"
#include "Fence.hpp"

#include "FxCommandPool.hpp"
#include "FxCommandBuffer.hpp"
#include "Descriptors.hpp"
#include "RvkImage.hpp"

#include "FxGpuBuffer.hpp"
#include <Math/Mat4.hpp>


namespace vulkan {

struct alignas(16) UniformBufferObject
{
    Mat4f MvpMatrix;
};

struct FrameData
{
public:
    void Create(RvkGpuDevice *device);
    void SubmitUbo(UniformBufferObject *ubo);
    void Destroy();
public:
    FxCommandPool CommandPool;
    RvkCommandBuffer CommandBuffer;

    DescriptorSet DescriptorSet;

    FxRawGpuBuffer<UniformBufferObject> UniformBuffer;

    // BLEH
    RvkImage DepthTexture;

    Semaphore ImageAvailable;
    Semaphore RenderFinished;
    Fence InFlight;
};

};
