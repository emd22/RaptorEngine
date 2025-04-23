#include "Renderer.hpp"
#include "Core/Defines.hpp"

#include <Renderer/FxRenderBackend.hpp>
#include <Core/FxPanic.hpp>

FX_SET_MODULE_NAME("Renderer")

FxRenderBackend *Renderer;
FxRenderBackendVulkan *RendererVulkan;

void SetRendererBackend(FxRenderBackend *backend)
{
    Renderer = backend;
    RendererVulkan = static_cast<FxRenderBackendVulkan *>(Renderer);
}

void AssertRendererExists()
{
    if (!Renderer->Initialized) {
        FxPanic("Renderer not initialized!", 0);
    }
}
