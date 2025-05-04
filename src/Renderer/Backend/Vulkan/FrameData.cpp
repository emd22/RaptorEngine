#include "FrameData.hpp"

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

namespace vulkan {

void FrameData::Create(GPUDevice *device)
{
    ImageAvailable.Create(device);
    RenderFinished.Create(device);

    InFlight.Create(device);

    UniformBuffer.Create(1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
}

void FrameData::Destroy()
{
    ImageAvailable.Destroy();
    RenderFinished.Destroy();

    InFlight.Destroy();

    UniformBuffer.Destroy();
};

} // namespace vulkan
