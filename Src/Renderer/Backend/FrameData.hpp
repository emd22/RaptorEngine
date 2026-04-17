#pragma once

#include "Commands.hpp"
#include "Descriptors.hpp"
#include "Synchro.hpp"

#include <Math/Mat4.hpp>

namespace fx::renderer {

struct alignas(16) UniformBufferObject
{
    Mat4f MvpMatrix;
};

struct FrameData
{
public:
    void Create(GpuDevice* device);
    void SubmitUbo(const UniformBufferObject& ubo);
    void Destroy();

public:
    CommandPool CommandPool;

    CommandBuffer CommandBuffer;

    // DescriptorSet DescriptorSet;
    DescriptorSet CompDescriptorSet;

    // RawGpuBuffer<UniformBufferObject> Ubo;

    Semaphore OffscreenSem;
    Semaphore LightingSem;
    Semaphore ShadowsSem;

    Semaphore ImageAvailable;
    Semaphore RenderFinished;
    Fence InFlight;
};

} // namespace fx::renderer
