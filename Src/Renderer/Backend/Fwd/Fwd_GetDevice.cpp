#include "Fwd_GetDevice.hpp"

#include <Renderer/Renderer.hpp>

RxGpuDevice* Fx_Fwd_GetGpuDevice()
{
    return Renderer->GetDevice();
}
