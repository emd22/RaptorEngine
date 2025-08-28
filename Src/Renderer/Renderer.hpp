#pragma once

#include <Renderer/RxRenderBackend.hpp>

void SetRendererBackend(RxRenderBackend* backend);

void AssertRendererExists();

extern RxRenderBackend* Renderer;
