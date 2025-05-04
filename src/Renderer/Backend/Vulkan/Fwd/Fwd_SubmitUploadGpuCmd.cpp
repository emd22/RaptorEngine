#include "Fwd_SubmitUploadGpuCmd.hpp"

#include <Renderer/Renderer.hpp>

namespace vulkan {

void Fx_Fwd_SubmitUploadCmd(std::function<void (FxCommandBuffer &)> func)
{
    RendererVulkan->SubmitUploadCmd(func);
}


}
