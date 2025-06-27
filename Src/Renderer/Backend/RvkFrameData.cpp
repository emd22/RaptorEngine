#include "RvkFrameData.hpp"

#include <vulkan/vulkan.h>

#include <Renderer/Renderer.hpp>

void RvkFrameData::Create(RvkGpuDevice *device)
{
    ImageAvailable.Create(device);
    RenderFinished.Create(device);

    InFlight.Create(device);

    Ubo.Create(1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    Ubo.Map();
}

void RvkFrameData::SubmitUbo(const RvkUniformBufferObject & ubo)
{
    memcpy(Ubo.MappedBuffer, &ubo, sizeof(RvkUniformBufferObject));
}

void RvkFrameData::Destroy()
{
    ImageAvailable.Destroy();
    RenderFinished.Destroy();

    InFlight.Destroy();

    Ubo.UnMap();
    Ubo.Destroy();
};
