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

    void DoCompPass(FxCamera& camera);

    void Destroy();
    ~RxDeferredRenderer() { Destroy(); }

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

    void CreateCompPass();
    void BuildLightDescriptors();


public:
    RxDescriptorPool DescriptorPool;

    /////////////////////
    // Geometry Pass
    /////////////////////

    VkDescriptorSetLayout DsLayoutGPassVertex = nullptr;
    VkDescriptorSetLayout DsLayoutGPassMaterial = nullptr;

    RxRenderStage GPass;

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

    RxRenderStage LightPass;

    RxPipeline PlLightingOutsideVolume;
    RxPipeline PlLightingInsideVolume;
    RxPipeline PlLightingDirectional;


    //////////////////////
    // Composition Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutCompFrag = nullptr;

    RxDescriptorSet DsComposition;

    RxRenderStage CompPass;

    RxPipeline PlComposition;
    RxPipeline PlCompositionUnlit;
};
