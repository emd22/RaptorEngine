#pragma once

#include "Backend/Descriptors.hpp"
#include "Backend/FrameData.hpp"
#include "Backend/Framebuffer.hpp"
#include "Backend/Image.hpp"
#include "Backend/Sampler.hpp"
#include "RenderStage.hpp"

namespace fx {
class Camera;
}

namespace fx::renderer {

struct FrameData;

class DeferredCompPass;
class DeferredLightingPass;


///////////////////////////////
// Main Deferred Renderer
///////////////////////////////

class DeferredRenderer
{
public:
    void Create(const Vec2u& extent);

    void DoCompPass(Camera& camera);

    void Destroy();
    ~DeferredRenderer() { Destroy(); }

private:
    // Geometry
    void CreateGPassPipeline();
    void DestroyGPassPipeline();

    void CreateGPass();

    VkPipelineLayout CreateGPassPipelineLayout();
    VkPipelineLayout CreateGPassSkinnedPipelineLayout();
    VkPipelineLayout CreateUnlitPipelineLayout();
    VkPipelineLayout CreateDebugLayerPipelineLayout();

    // Lighting
    // void CreateLightVolumePipeline();
    void CreateLightingPipeline();

    void CreateLightingDSLayout();

    // void DestroyLightVolumePipeline();
    void DestroyLightingPipeline();

    // PipelineLayout CreateLightingPipelineLayout();

    // Composition
    void CreateCompPipeline();
    void DestroyCompPipeline();

    void CreateUnlitPipeline();

    PipelineLayout CreateCompPipelineLayout();

    void CreateCompPass();
    void BuildLightDescriptors();

public:
    DescriptorPool DescriptorPool;

    /////////////////////
    // Geometry Pass
    /////////////////////

    VkDescriptorSetLayout DsLayoutGPassMaterial = nullptr;
    VkDescriptorSetLayout DsLayoutGPassSkinned = nullptr;

    VkDescriptorSetLayout DsLayoutGPassMaterialAlbedoOnly = nullptr;

    RenderStage GPass;

    // Pipeline PlGeometry;
    // Pipeline PlGeometryNoDepthTest;
    // Pipeline PlGeometryWithNormalMaps;

    // Pipeline PlGeometrySkinned;

    Pipeline* pGeometryPipeline = nullptr;

    //////////////////////
    // Lighting Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutLightingFrag = nullptr;
    VkDescriptorSetLayout DsLayoutLightingMaterialProperties = nullptr;

    DescriptorSet DsLighting;

    RenderStage LightPass;

    // Pipeline PlLightingOutsideVolume;
    // Pipeline PlLightingInsideVolume;
    // Pipeline PlLightingDirectional;

    /////////////////////////////////////////////////
    // Forward pass / Unlit
    /////////////////////////////////////////////////
    // VkDescriptorSetLayout DsLayoutUnlit = nullptr;
    Pipeline PlText;
    // Pipeline PlDebugLayer;
    // DescriptorSet DsUnlit;

    RenderPass RpForward;
    Framebuffer FbForward;

    //////////////////////
    // Composition Pass
    //////////////////////

    VkDescriptorSetLayout DsLayoutCompFrag = nullptr;

    DescriptorSet DsComposition;

    RenderStage CompPass;

    // Pipeline PlComposition;
    // Pipeline PlCompositionUnlit;
};

} // namespace fx::renderer
