#pragma once

#include "Backend/Descriptors.hpp"
#include "PipelineNames.hpp"
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

	void CreateUnlitPass();
	void CreateGPass();


	// Lighting
	// void CreateLightVolumePipeline();
	void CreateLightingPipeline();
	void CreateLightingDSLayout();

	// Composition
	void CreateCompPipeline();

	void CreateUnlitPipeline();

public:
	DescriptorPool DescriptorPool;

	/////////////////////
	// Geometry Pass
	/////////////////////

	// VkDescriptorSetLayout DsLayoutGPassMaterial = nullptr;
	// VkDescriptorSetLayout DsLayoutGPassSkinned = nullptr;

	// VkDescriptorSetLayout DsLayoutGPassMaterialAlbedoOnly = nullptr;

	RenderStage UnlitPass;
	RenderStage LightPass;
	RenderStage GPass;
	RenderStage ForwardPass;
	RenderStage CompPass;

	// Pipeline PlGeometry;
	// Pipeline PlGeometryNoDepthTest;
	// Pipeline PlGeometryWithNormalMaps;

	// Pipeline PlGeometrySkinned;

	ePipelineName pGeometryPipelineName = ePipelineName::Geometry;

	//////////////////////
	// Lighting Pass
	//////////////////////

	// VkDescriptorSetLayout DsLayoutLightingFrag = nullptr;
	// VkDescriptorSetLayout DsLayoutLightingMaterialProperties = nullptr;

	// DescriptorSet DsLighting;


	// Pipeline PlLightingOutsideVolume;
	// Pipeline PlLightingInsideVolume;
	// Pipeline PlLightingDirectional;

	/////////////////////////////////////////////////
	// Forward pass / Unlit
	/////////////////////////////////////////////////
	// VkDescriptorSetLayout DsLayoutUnlit = nullptr;
	// Pipeline PlText;
	// Pipeline PlDebugLayer;
	// DescriptorSet DsUnlit;

	/*    RenderPass RpForward;
		Framebuffer FbForward;*/

	//////////////////////
	// Composition Pass
	//////////////////////

	// VkDescriptorSetLayout DsLayoutCompFrag = nullptr;

	// DescriptorSet DsComposition;

	// Pipeline PlComposition;
	// Pipeline PlCompositionUnlit;
};

} // namespace fx::renderer
