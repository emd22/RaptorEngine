#pragma once

#include "Backend/RxDescriptors.hpp"
#include "Backend/RxFrameData.hpp"
#include "Backend/RxFramebuffer.hpp"
#include "Backend/RxImage.hpp"
#include "Backend/RxSampler.hpp"

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

    FX_FORCE_INLINE RxDeferredGPass* GetCurrentGPass();
    FX_FORCE_INLINE RxDeferredCompPass* GetCurrentCompPass();
    FX_FORCE_INLINE RxDeferredLightingPass* GetCurrentLightingPass();

    void RebuildLightingPipeline();

private:
    // Geometry
    void CreateGPassPipeline();
    void DestroyGPassPipeline();

    FX_FORCE_INLINE VkPipelineLayout CreateGPassPipelineLayout();

    // Lighting
    void CreateLightVolumePipeline();
    void CreateLightingPipeline();

    void CreateLightingDSLayout();

    void DestroyLightVolumePipeline();
    void DestroyLightingPipeline();

    FX_FORCE_INLINE VkPipelineLayout CreateLightingPipelineLayout();
    FX_FORCE_INLINE VkPipelineLayout CreateLightPassPipelineLayout();

    // Composition
    void CreateCompPipeline();
    void DestroyCompPipeline();

    FX_FORCE_INLINE VkPipelineLayout CreateCompPipelineLayout();


public:
    /////////////////////
    // Geometry Pass
    /////////////////////

    VkDescriptorSetLayout DsLayoutGPassVertex = nullptr;
    VkDescriptorSetLayout DsLayoutGPassMaterial = nullptr;

    RxGraphicsPipeline GPassPipeline;
    RxRenderPass RpGeometry;

    FxSizedArray<RxDeferredGPass> GPasses;

    //////////////////////
    // Lighting Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutLightingFrag = nullptr;
    VkDescriptorSetLayout DsLayoutLightingMaterialProperties = nullptr;

    RxGraphicsPipeline LightingPipeline;
    RxRenderPass RpLighting;

    FxSizedArray<RxDeferredLightingPass> LightingPasses;

    //////////////////////
    // Composition Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutCompFrag = nullptr;

    RxGraphicsPipeline CompPipeline;
    RxRenderPass RpComposition;

    FxSizedArray<RxFramebuffer> OutputFramebuffers;

    FxSizedArray<RxDeferredCompPass> CompPasses;
};


///////////////////////////////
// Geometry Pass (Per FIF)
///////////////////////////////

class RxDeferredGPass
{
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
    RxGraphicsPipeline* mGPassPipeline = nullptr;
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
    RxGraphicsPipeline* mLightingPipeline = nullptr;
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
    RxGraphicsPipeline* mCompPipeline = nullptr;
    RxRenderPass* mRenderPass = nullptr;
    RxDeferredRenderer* mRendererInst = nullptr;

    RxFrameData* mCurrentFrame = nullptr;
};
