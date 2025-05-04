#include "FxGpuBuffer.hpp"

#include <Renderer/Renderer.hpp>

namespace vulkan {

void FxGpuBufferSubmitUploadCmd(std::function<void(FxCommandBuffer &)> func)
{
    RendererVulkan->SubmitUploadCmd(func);
}

} // namespace vulkan
