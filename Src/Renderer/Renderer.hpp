#pragma once

#include <Renderer/FxRenderBackend.hpp>

void SetRendererBackend(FxRenderBackend *backend);

void AssertRendererExists();

extern FxRenderBackend *Renderer;
