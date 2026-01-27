#pragma once

#include "Backend/RxImage.hpp"
#include "Backend/RxPipeline.hpp"
#include "Backend/RxSampler.hpp"
#include "FxPrimitiveMesh.hpp"

#include <vulkan/vulkan.h>

#include <Core/FxRef.hpp>
#include <Math/FxVec2.hpp>

class FxCamera;
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

    void Render(const RxCommandBuffer& cmd, const FxCamera& render_cam);

    void Destroy();

private:
    void CreateSkyboxPipeline();
    // VkPipelineLayout CreateSkyboxPipelineLayout();
    void BuildDescriptorSets(uint32 frame_index);

public:
    // VkDescriptorSetLayout DsLayoutSkyboxVertex = nullptr;
    VkDescriptorSetLayout DsLayoutSkyboxFragment = nullptr;

    FxStackArray<RxDescriptorSet, RxFramesInFlight> DsSkyboxFragments {};

    RxPipeline SkyboxPipeline;

    FxStackArray<RxImage*, RxFramesInFlight> SkyboxAttachments;

    RxImage* SkyAttachment = nullptr;

private:
    // RxSampler mSkySampler;

    RxDescriptorPool mDescriptorPool {};

    FxRef<FxPrimitiveMesh<VertexType>> mSkyboxMesh { nullptr };
};
