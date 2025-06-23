#include "Fwd_GetDevice.hpp"

#include <Renderer/Renderer.hpp>


RvkGpuDevice* Fx_Fwd_GetGpuDevice()
{
    return Renderer->GetDevice();
}
