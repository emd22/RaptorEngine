#include "FrameData.hpp"

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <Renderer/Renderer.hpp>

namespace vulkan {

void FrameData::Create(RvkGpuDevice *device)
{
    ImageAvailable.Create(device);
    RenderFinished.Create(device);

    InFlight.Create(device);

    UniformBuffer.Create(1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    UniformBuffer.Map();
}

void FrameData::SubmitUbo(UniformBufferObject *ubo)
{
    memcpy(UniformBuffer.MappedBuffer, ubo, sizeof(UniformBufferObject));
}

void FrameData::Destroy()
{
    ImageAvailable.Destroy();
    RenderFinished.Destroy();

    InFlight.Destroy();

    UniformBuffer.UnMap();
    UniformBuffer.Destroy();
};

} // namespace vulkan
