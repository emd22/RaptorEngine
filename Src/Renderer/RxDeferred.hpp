#pragma once

#include "Backend/RxDescriptors.hpp"
#include "Backend/RxFrameData.hpp"
#include "Backend/RxFramebuffer.hpp"
#include "Backend/RxImage.hpp"
#include "Backend/RxSampler.hpp"
#include "RxPipelineList.hpp"
#include "RxRenderPassCache.hpp"
#include "RxSkybox.hpp"

struct RxFrameData;

class RxDeferredGPass;
class RxDeferredCompPass;
class RxDeferredLightingPass;


///////////////////////////////
// Main Deferred Renderer
///////////////////////////////

class RxDeferredRenderer
{
public:
    void Create(const FxVec2u& extent);

    void Destroy();

    ~RxDeferredRenderer() { Destroy(); }

    void ToggleWireframe(bool enable);

    RxDeferredGPass* GetCurrentGPass();
    RxDeferredCompPass* GetCurrentCompPass();
    RxDeferredLightingPass* GetCurrentLightingPass();

    void RebuildLightingPipeline();

private:
    // Geometry
    void CreateGPassPipeline();
    void DestroyGPassPipeline();

    VkPipelineLayout CreateGPassPipelineLayout();

    // Lighting
    // void CreateLightVolumePipeline();
    void CreateLightingPipeline();

    void CreateLightingDSLayout();

    // void DestroyLightVolumePipeline();
    void DestroyLightingPipeline();

    VkPipelineLayout CreateLightingPipelineLayout();
    VkPipelineLayout CreateLightPassPipelineLayout();

    // Composition
    void CreateCompPipeline();
    void DestroyCompPipeline();

    VkPipelineLayout CreateCompPipelineLayout();


public:
    /////////////////////
    // Geometry Pass
    /////////////////////

    VkDescriptorSetLayout DsLayoutGPassVertex = nullptr;
    VkDescriptorSetLayout DsLayoutGPassMaterial = nullptr;

    // VkDescriptorSetLayout DsLayoutObjectBuffer = nullptr;

    RxGraphicsPipeline PlGeometry;
    RxGraphicsPipeline PlGeometryNoDepthTest;
    RxGraphicsPipeline PlGeometryWireframe;
    RxGraphicsPipeline PlGeometryWithNormalMaps;

    RxGraphicsPipeline* pGeometryPipeline = nullptr;

    RxRenderPass RpGeometry;
    // RxRenderPassId RpGeometryId = 0;

    FxSizedArray<RxDeferredGPass> GPasses;

    //////////////////////
    // Lighting Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutLightingFrag = nullptr;
    VkDescriptorSetLayout DsLayoutLightingMaterialProperties = nullptr;

    RxGraphicsPipeline PlLightingOutsideVolume;
    RxGraphicsPipeline PlLightingInsideVolume;

    RxRenderPass RpLighting;

    FxSizedArray<RxDeferredLightingPass> LightingPasses;

    //////////////////////
    // Composition Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutCompFrag = nullptr;

    RxGraphicsPipeline PlComposition;
    RxGraphicsPipeline PlCompositionUnlit;
    RxRenderPass RpComposition;

    FxSizedArray<RxFramebuffer> OutputFramebuffers;

    FxSizedArray<RxDeferredCompPass> CompPasses;


    // RxSkyboxRenderer SkyboxRenderer;
};


///////////////////////////////
// Geometry Pass (Per FIF)
///////////////////////////////

class RxDeferredGPass
{
    friend class RxDeferredRenderer;

public:
    void Create(RxDeferredRenderer* renderer, const FxVec2u& extent);
    void Destroy();

    void Begin();
    void End();
    void Submit();

    void SubmitUniforms(const RxUniformBufferObject& ubo);
    void BuildDescriptorSets();

public:
    // G-Pass attachments
    RxImage ColorAttachment;
    RxImage PositionsAttachment;
    RxImage NormalsAttachment;

    RxImage DepthAttachment;

    // G-Pass samplers
    RxSampler ColorSampler;
    RxSampler PositionsSampler;

    RxFramebuffer Framebuffer;

    RxDescriptorPool DescriptorPool;
    //    RxDescriptorSet DescriptorSet;

    RxRawGpuBuffer<RxUniformBufferObject> UniformBuffer;


private:
    RxGraphicsPipeline* mPlGeometry = nullptr;
    RxRenderPass* mRenderPass = nullptr;
    RxDeferredRenderer* mRendererInst = nullptr;
};

///////////////////////////////
// Lighting Pass (Per FIF)
///////////////////////////////

class RxDeferredLightingPass
{
public:
    void Create(RxDeferredRenderer* renderer, uint16 frame_index, const FxVec2u& extent);
    void Destroy();

    void Begin();
    void End();
    void Submit();

    void BuildDescriptorSets(uint16 frame_index);

public:
    RxImage ColorAttachment;

    RxFramebuffer Framebuffer;

    RxDescriptorPool DescriptorPool;
    RxDescriptorSet DescriptorSet;

private:
    RxGraphicsPipeline* mPlLighting = nullptr;
    RxRenderPass* mRenderPass = nullptr;
    RxDeferredRenderer* mRendererInst = nullptr;
};


class FxCamera;

///////////////////////////////
// Composition Pass (Per FIF)
///////////////////////////////

class RxDeferredCompPass
{
public:
    void Create(RxDeferredRenderer* renderer, uint16 frame_index, const FxVec2u& extent);
    void Destroy();

    void Begin();
    void DoCompPass(FxCamera& render_cam);

    void BuildDescriptorSets(uint16 frame_index);

public:
    RxFramebuffer Framebuffer;

    RxImage* OutputImage;

    RxDescriptorPool DescriptorPool;
    RxDescriptorSet DescriptorSet;

private:
    RxGraphicsPipeline* mPlComposition = nullptr;
    RxRenderPass* mRenderPass = nullptr;
    RxDeferredRenderer* mRendererInst = nullptr;

    RxFrameData* mCurrentFrame = nullptr;
};
