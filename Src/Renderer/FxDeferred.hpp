#pragma once

#include "Backend/RvkImage.hpp"
#include "Backend/RvkSampler.hpp"
#include "Backend/RvkFramebuffer.hpp"
#include "Backend/RvkDescriptors.hpp"

#include "Backend/RvkFrameData.hpp"


class FxDeferredRenderer;
struct RvkFrameData;

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

    void SubmitUniforms(const RvkUniformBufferObject& ubo);
    void BuildDescriptorSets();

public:
    // G-Pass attachments
    RvkImage ColorAttachment;
    RvkImage PositionsAttachment;
    RvkImage NormalsAttachment;

    RvkImage DepthAttachment;

    // G-Pass samplers
    RvkSampler ColorSampler;
    RvkSampler PositionsSampler;

    RvkFramebuffer Framebuffer;

    RvkDescriptorPool DescriptorPool;
    RvkDescriptorSet DescriptorSet;

    RvkRawGpuBuffer<RvkUniformBufferObject> UniformBuffer;


private:
    RvkGraphicsPipeline* mGPassPipeline = nullptr;
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
    RvkImage ColorAttachment;

    RvkFramebuffer Framebuffer;

    RvkDescriptorPool DescriptorPool;
    RvkDescriptorSet DescriptorSet;

private:
    RvkGraphicsPipeline* mLightingPipeline = nullptr;
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
    RvkImage ColorAttachment;

    RvkFramebuffer Framebuffer;

    RvkDescriptorPool DescriptorPool;
    RvkDescriptorSet DescriptorSet;

private:
    RvkGraphicsPipeline* mLightingPipeline = nullptr;
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
    RvkFramebuffer Framebuffer;

    RvkImage* OutputImage;

    RvkDescriptorPool DescriptorPool;
    RvkDescriptorSet DescriptorSet;

private:
    RvkGraphicsPipeline* mCompPipeline = nullptr;
    FxDeferredRenderer* mRendererInst = nullptr;

    RvkFrameData* mCurrentFrame = nullptr;
};


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

    RvkGraphicsPipeline GPassPipeline;

    FxSizedArray<FxDeferredGPass> GPasses;

    //////////////////////
    // Lighting Pass
    //////////////////////

    RvkGraphicsPipeline LightVolumesPipeline;
    FxSizedArray<FxDeferredLightVolumePass> LightVolumePasses;

    VkDescriptorSetLayout DsLayoutLightingFrag = nullptr;
    RvkGraphicsPipeline LightingPipeline;

    FxSizedArray<FxDeferredLightingPass> LightingPasses;

    //////////////////////
    // Composition Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutCompFrag = nullptr;

    RvkGraphicsPipeline CompPipeline;
    FxSizedArray<RvkFramebuffer> OutputFramebuffers;

    FxSizedArray<FxDeferredCompPass> CompPasses;
};
