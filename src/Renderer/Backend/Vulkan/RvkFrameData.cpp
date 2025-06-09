#include "RvkFrameData.hpp"

#include <vulkan/vulkan.h>

#include <Renderer/Renderer.hpp>

namespace vulkan {

void RvkFrameData::Create(RvkGpuDevice *device)
{
    ImageAvailable.Create(device);
    RenderFinished.Create(device);

    InFlight.Create(device);

    UniformBuffer.Create(1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    UniformBuffer.Map();
}

void RvkFrameData::SubmitUbo(RvkUniformBufferObject *ubo)
{
    memcpy(UniformBuffer.MappedBuffer, ubo, sizeof(RvkUniformBufferObject));
}

void RvkFrameData::Destroy()
{
    ImageAvailable.Destroy();
    RenderFinished.Destroy();

    InFlight.Destroy();

    UniformBuffer.UnMap();
    UniformBuffer.Destroy();
};

} // namespace vulkan
