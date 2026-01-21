#pragma once

#include "Backend/RxDescriptors.hpp"
#include "Backend/RxFrameData.hpp"
#include "Backend/RxFramebuffer.hpp"
#include "Backend/RxImage.hpp"
#include "Backend/RxSampler.hpp"
#include "RxPipelineList.hpp"
#include "RxRenderPassCache.hpp"
#include "RxRenderStage.hpp"
#include "RxSkybox.hpp"

struct RxFrameData;

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

    RxDeferredCompPass* GetCurrentCompPass();
    RxDeferredLightingPass* GetCurrentLightingPass();

    void RebuildLightingPipeline();

private:
    // Geometry
    void CreateGPassPipeline();
    void DestroyGPassPipeline();

    void CreateGPass();

    VkPipelineLayout CreateGPassPipelineLayout();

    // Lighting
    // void CreateLightVolumePipeline();
    void CreateLightingPipeline();

    void CreateLightingDSLayout();

    // void DestroyLightVolumePipeline();
    void DestroyLightingPipeline();

    VkPipelineLayout CreateLightingPipelineLayout();

    // Composition
    void CreateCompPipeline();
    void DestroyCompPipeline();

    VkPipelineLayout CreateCompPipelineLayout();

    void BuildCompDescriptors();
    void BuildLightDescriptors();


public:
    RxDescriptorPool DescriptorPool;

    /////////////////////
    // Geometry Pass
    /////////////////////

    VkDescriptorSetLayout DsLayoutGPassVertex = nullptr;
    VkDescriptorSetLayout DsLayoutGPassMaterial = nullptr;

    RxRenderStage<3> GPass;
    
    RxPipeline PlGeometry;
    RxPipeline PlGeometryNoDepthTest;
    RxPipeline PlGeometryWireframe;
    RxPipeline PlGeometryWithNormalMaps;

    RxPipeline* pGeometryPipeline = nullptr;

    //////////////////////
    // Lighting Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutLightingFrag = nullptr;
    VkDescriptorSetLayout DsLayoutLightingMaterialProperties = nullptr;

    RxDescriptorSet DsLighting;

    RxRenderStage<1> LightPass;

    RxPipeline PlLightingOutsideVolume;
    RxPipeline PlLightingInsideVolume;

    RxPipeline PlLightingDirectional;


    //////////////////////
    // Composition Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutCompFrag = nullptr;

    RxRenderStage<1> CompPass;

    RxPipeline PlComposition;
    RxPipeline PlCompositionUnlit;
    RxRenderPass RpComposition;

    FxSizedArray<RxFramebuffer> OutputFramebuffers;

    FxSizedArray<RxDeferredCompPass> CompPasses;


    // RxSkyboxRenderer SkyboxRenderer;
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
    RxPipeline* mPlComposition = nullptr;
    RxRenderPass* mRenderPass = nullptr;
    RxDeferredRenderer* mRendererInst = nullptr;

    RxFrameData* mCurrentFrame = nullptr;
};
