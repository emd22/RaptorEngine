#include "RvkGpuBuffer.hpp"

#include <Renderer/Renderer.hpp>

namespace vulkan {

void RvkGpuBufferSubmitUploadCmd(std::function<void(RvkCommandBuffer &)> func)
{
    RendererVulkan->SubmitUploadCmd(func);
}

} // namespace vulkan
