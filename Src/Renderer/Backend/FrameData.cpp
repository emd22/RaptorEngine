#include "FrameData.hpp"

#include <vulkan/vulkan.h>

namespace fx::renderer {

void FrameData::Create(GpuDevice* device)
{
    ImageAvailable.Create();
    RenderFinished.Create();
    OffscreenSem.Create();
    LightingSem.Create();
    ShadowsSem.Create();

    InFlight.Create();

    // Ubo.Create(1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    // Ubo.Map();
}

void FrameData::SubmitUbo(const UniformBufferObject& ubo)
{
    // memcpy(Ubo.MappedBuffer, &ubo, sizeof(UniformBufferObject));
}

void FrameData::Destroy()
{
    ImageAvailable.Destroy();
    RenderFinished.Destroy();
    OffscreenSem.Destroy();
    LightingSem.Destroy();
    ShadowsSem.Destroy();

    InFlight.Destroy();

    // Ubo.UnMap();
    // Ubo.Destroy();
};

} // namespace fx::renderer
