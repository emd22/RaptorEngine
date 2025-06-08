#include "Fwd_GetDevice.hpp"

#include <Renderer/Renderer.hpp>

namespace vulkan {

RvkGpuDevice* Fx_Fwd_GetGpuDevice()
{
    return RendererVulkan->GetDevice();
}

}
