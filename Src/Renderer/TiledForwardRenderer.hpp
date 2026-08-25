#pragma once

#include "Backend/Descriptors.hpp"
#include "PipelineNames.hpp"
#include "RenderStage.hpp"

namespace fx {
class Camera;
}

namespace fx::renderer {

struct FrameData;
class CommandBuffer;


struct alignas(16) CompositionPushConsts
{
	uint32 FrameExtent[2];
};


///////////////////////////////
// Main Deferred Renderer
///////////////////////////////

class TiledForwardRenderer
{
public:
	void Create(const Vec2u& extent);

	void RenderComposition(Camera& camera);

	/**
	 * @brief Dispatches the Forward+ light culling pass. Must be called outside of a renderpass,
	 * after all lights have been submitted for the frame.
	 */
	void DoLightCullingPass(Camera& camera);

	/// Binds the Forward+ tiled light list descriptor set (set 2) on the geometry pipeline
	void BindLightGridDescriptors(CommandBuffer& cmd);

	void Destroy();
	~TiledForwardRenderer() { Destroy(); }

private:
	// Geometry
	void CreateForwardPSO();
	void CreateSSAOPSO();

	void BuildPersistentDescriptor();

	void CreateGPass();
	void CreateSSAOPass();


	// Lighting
	// void CreateLightVolumePipeline();
	void CreateLightingPipeline();
	void CreateLightingDSLayout();

	// Light culling
	void CreateLightCullingPSO();

	/// Registers the Forward+ tiled light list buffers (set 2) on the pipeline currently being built
	void AddLightGridDescriptors();

	// Composition
	void CreateCompositionPSO();

	void GenerateRandomTexture(uint32 size);


public:
	DescriptorPool DescriptorPool;

	FX_FORCE_INLINE uint32 GetLightTileColumns() const { return mLightTileColumns; }

	RenderStage LightPass;
	RenderStage ForwardPass;
	RenderStage SSAOPass;
	RenderStage CompPass;

	ePipelineName pGeometryPipelineName = ePipelineName::Geometry;

	/// Descriptors that remain bound for the entirety of the frame. This includes object buffer, material buffer, etc.
	DescriptorSet* pPersistentDescriptor = nullptr;

	/// Persistent descriptor that only applies for the object and material buffers.
	DescriptorSet* pPersistentDescriptorSlim = nullptr;

	/// Amount of tile columns the light grid is dispatched with for the current frame
	uint32 mLightTileColumns = 0;
};

} // namespace fx::renderer
