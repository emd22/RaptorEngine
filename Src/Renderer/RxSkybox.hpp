#pragma once

#include "Backend/RxImage.hpp"
#include "Backend/RxPipeline.hpp"
#include "Backend/RxSampler.hpp"
#include "FxPrimitiveMesh.hpp"

#include <vulkan/vulkan.h>

#include <Math/FxVec2.hpp>

class RxDeferredRenderer;

struct alignas(16) RxSkyboxPushConstants
{
    float32 ProjectionMatrix[16];
    float32 ModelMatrix[16];
};

class RxSkyboxRenderer
{
public:
    using VertexType = RxVertex<FxVertexPosition>;

public:
    void Create(const FxVec2u& extent, const FxRef<FxPrimitiveMesh<VertexType>>& skybox_mesh);

    void Render();

    void Destroy();

private:
    void CreateSkyboxPipeline();
    VkPipelineLayout CreateSkyboxPipelineLayout();

public:
    // VkDescriptorSetLayout DsLayoutSkyboxVertex = nullptr;
    VkDescriptorSetLayout DsLayoutSkyboxFragment = nullptr;

    RxGraphicsPipeline SkyboxPipeline;

private:
    RxSampler mSkySampler;
    RxImage mSky;

    FxRef<FxPrimitiveMesh<VertexType>> mSkyboxMesh { nullptr };

    RxDeferredRenderer* mDeferredRenderer = nullptr;
};
