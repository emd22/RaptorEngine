#pragma once

#include <Renderer/FxRenderBackend.hpp>
#include <Renderer/Backend/FxRenderBackendVulkan.hpp>

void SetRendererBackend(FxRenderBackend *backend);

void AssertRendererExists();

extern FxRenderBackend *Renderer;
extern vulkan::FxRenderBackendVulkan *RendererVulkan;
