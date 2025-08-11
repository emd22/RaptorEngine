#pragma once

#include "Backend/RxImage.hpp"
#include "Backend/RxSampler.hpp"
#include "Backend/RxFramebuffer.hpp"
#include "Backend/RxDescriptors.hpp"

#include "Backend/RxFrameData.hpp"

struct RxFrameData;

class FxDeferredGPass;
class FxDeferredCompPass;
class FxDeferredLightingPass;


///////////////////////////////
// Main Deferred Renderer
///////////////////////////////

class FxDeferredRenderer
{
public:
    void Create(const FxVec2u& extent);

    void Destroy();

    ~FxDeferredRenderer()
    {
        Destroy();
    }

    FX_FORCE_INLINE FxDeferredGPass* GetCurrentGPass();
    FX_FORCE_INLINE FxDeferredCompPass* GetCurrentCompPass();
    FX_FORCE_INLINE FxDeferredLightingPass* GetCurrentLightingPass();

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

    FxSizedArray<FxDeferredGPass> GPasses;

    //////////////////////
    // Lighting Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutLightingFrag = nullptr;
    RxGraphicsPipeline LightingPipeline;

    FxSizedArray<FxDeferredLightingPass> LightingPasses;

    //////////////////////
    // Composition Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutCompFrag = nullptr;

    RxGraphicsPipeline CompPipeline;
    FxSizedArray<RxFramebuffer> OutputFramebuffers;

    FxSizedArray<FxDeferredCompPass> CompPasses;
};



///////////////////////////////
// Geometry Pass (Per FIF)
///////////////////////////////

class FxDeferredGPass
{
public:
    void Create(FxDeferredRenderer* renderer, const FxVec2u& extent);
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
    RxDescriptorSet DescriptorSet;

    RxRawGpuBuffer<RxUniformBufferObject> UniformBuffer;


private:
    RxGraphicsPipeline* mGPassPipeline = nullptr;
    FxDeferredRenderer* mRendererInst = nullptr;
};


class FxDeferredLightVolumePass
{
public:
    void Create(FxDeferredRenderer* renderer, uint16 frame_index, const FxVec2u& extent);
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
    FxDeferredRenderer* mRendererInst = nullptr;
};

///////////////////////////////
// Lighting Pass (Per FIF)
///////////////////////////////

class FxDeferredLightingPass
{
public:
    void Create(FxDeferredRenderer* renderer, uint16 frame_index, const FxVec2u& extent);
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
    FxDeferredRenderer* mRendererInst = nullptr;
};


class FxCamera;

///////////////////////////////
// Composition Pass (Per FIF)
///////////////////////////////

class FxDeferredCompPass
{
public:
    void Create(FxDeferredRenderer* renderer, uint16 frame_index, const FxVec2u& extent);
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
    FxDeferredRenderer* mRendererInst = nullptr;

    RxFrameData* mCurrentFrame = nullptr;
};
