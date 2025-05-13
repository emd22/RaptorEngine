#include "Fwd_GetGpuAllocator.hpp"

#include <Renderer/Renderer.hpp>

VmaAllocator Fx_Fwd_GetGpuAllocator()
{
    return RendererVulkan->GpuAllocator;
}
