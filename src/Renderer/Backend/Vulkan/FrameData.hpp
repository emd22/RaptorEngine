#pragma once

#include "Semaphore.hpp"
#include "Fence.hpp"

#include "FxCommandPool.hpp"
#include "FxCommandBuffer.hpp"
#include "Descriptors.hpp"

#include "FxGpuBuffer.hpp"
#include <Math/Mat4.hpp>


namespace vulkan {

struct UniformBufferObject
{
    Mat4f MvpMatrix;
};

class FrameData
{
public:
    void Create(GPUDevice *device);
    void SubmitUbo(UniformBufferObject *ubo);
    void Destroy();
public:
    FxCommandPool CommandPool;
    FxCommandBuffer CommandBuffer;

    DescriptorSet DescriptorSet;

    FxRawGpuBuffer<UniformBufferObject> UniformBuffer;

    Semaphore ImageAvailable;
    Semaphore RenderFinished;
    Fence InFlight;
};

};
