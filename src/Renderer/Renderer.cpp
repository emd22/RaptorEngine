#include "Renderer.hpp"
#include "Core/Defines.hpp"

#include <Core/FxPanic.hpp>

FX_SET_MODULE_NAME("Renderer")

FxRenderBackend *Renderer;
vulkan::FxRenderBackendVulkan *RendererVulkan;

void SetRendererBackend(FxRenderBackend *backend)
{
    Renderer = backend;
    RendererVulkan = static_cast<vulkan::FxRenderBackendVulkan *>(Renderer);
}

void AssertRendererExists()
{
    if (!Renderer->Initialized) {
        FxPanic("Renderer not initialized!", 0);
    }
}
