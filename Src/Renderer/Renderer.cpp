#include "Renderer.hpp"

#include "Core/FxDefines.hpp"

#include <Core/FxPanic.hpp>

FX_SET_MODULE_NAME("Renderer")

FxRenderBackend* Renderer;

void SetRendererBackend(FxRenderBackend* backend)
{
    Renderer = backend;
    Renderer = static_cast<FxRenderBackend*>(Renderer);
}

void AssertRendererExists()
{
    if (!Renderer->Initialized) {
        FxModulePanic("Renderer not initialized!", 0);
    }
}
