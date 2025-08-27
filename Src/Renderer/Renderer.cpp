#include "Renderer.hpp"

#include "Core/FxDefines.hpp"

#include <Core/FxPanic.hpp>

FX_SET_MODULE_NAME("Renderer")

RxRenderBackend* Renderer;

void SetRendererBackend(RxRenderBackend* backend)
{
    Renderer = backend;
    Renderer = static_cast<RxRenderBackend*>(Renderer);
}

void AssertRendererExists()
{
    if (!Renderer->Initialized) {
        FxModulePanic("Renderer not initialized!", 0);
    }
}
