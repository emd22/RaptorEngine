#pragma once

#include "Backend/Descriptors.hpp"
#include "PipelineNames.hpp"
#include "RenderStage.hpp"

namespace fx {
class Camera;
class Image;
} // namespace fx

namespace fx::renderer {

struct FrameData;
class CommandBuffer;


struct alignas(16) CompositionPushConsts
{
	uint32 FrameExtent[2];
};

struct alignas(16) SSAOPushConsts
{
	float32 InvProjection[16];
	float32 Projection[16];
	float32 View[16];
	float32 RenderSize[2];
	float32 Radius;
	float32 Bias;
};

struct alignas(16) SSAOBlurPushConsts
{
	float32 ScreenSize[2];
	float32 TexelSize[2];
	float32 DepthSharpness;
};


/**
 * @brief Culls a different view of the light buffer than what was submitted for the frame. Probe captures use this to
 * swap the sun for a copy of it whose shadow map is centered on the probe, see World::RenderProbeCapture().
 */
struct LightCullOverride
{
	/// Number of light buffer slots to cull, starting from the first
	uint32 LightCount = 0;
	/// Light that is swapped out for `ReplacementSlot`, UINT32_MAX for none
	uint32 ReplacedLight = UINT32_MAX;
	uint32 ReplacementSlot = 0;
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
	 * @param pExtentOverride Tile grid is derived from this extent instead of the swapchain
	 * (used by probe capture bakes, which render at a fixed small size).
	 * @param pLightOverride Culls these lights instead of every light submitted this frame.
	 */
	void DoLightCullingPass(Camera& camera, const Vec2u* pExtentOverride = nullptr,
							const LightCullOverride* pLightOverride = nullptr);

	/**
	 * @brief Sets the atlases that decals are sampled from, rebuilding the persistent descriptor set when either
	 * changes. Null binds a blank image.
	 */
	void SetDecalAtlases(Image* atlas, Image* normal_atlas);

	void Destroy();
	~TiledForwardRenderer() { Destroy(); }

private:
	// Geometry
	void CreateForwardPSO();
	void CreateDepthNormalPSO();
	void CreateSSAOPSO();
	void CreateSSAOBlurPSO();
	void CreateDebugLayerPSO();
	void CreateDebugSolidPSO();

	void BuildPersistentDescriptor();

	void CreateForwardPass();
	void CreateDepthNormalPass();
	void CreateSSAOPass();
	void CreateSSAOBlurPass();

	// Lighting
	// void CreateLightVolumePipeline();
	void CreateLightingPipeline();
	void CreateLightingDSLayout();

	// Light culling
	void CreateLightCullingPSO();

	/// Registers the decal buffers and atlases (set 0) on the forward pipeline currently being built
	void AddDecalDescriptors();

	// Composition
	void CreateCompositionPSO();
	void CreateBitmapTextPSO();

	void GenerateRandomTexture(uint32 size);


public:
	DescriptorPool DescriptorPool;

	FX_FORCE_INLINE uint32 GetLightTileColumns() const { return mLightTileColumns; }
	FX_FORCE_INLINE uint32 GetLightTileRows() const { return mLightTileRows; }

	/// Depth + Normal prepass
	RenderStage Prepass;
	/// Main Forward+ lit pass
	RenderStage ForwardPass;
	/// SSAO pass
	RenderStage SSAOPass;
	/// SSAO blur pass
	RenderStage SSAOBlurPass;
	/// Composition pass, combine results from all passes
	RenderStage CompPass;

	ePipelineName pGeometryPipelineName = ePipelineName::Geometry;

	/// Descriptors that remain bound for the entirety of the frame. This includes object buffer, material buffer, etc.
	DescriptorSet* pPersistentDescriptor = nullptr;

	/// Persistent descriptor that only applies for the object and material buffers.
	DescriptorSet* pPersistentDescriptorSlim = nullptr;

	/// Amount of tile columns the light grid is dispatched with for the current frame
	uint32 mLightTileColumns = 0;
	uint32 mLightTileRows = 0;

private:
	/// Decal atlases that the persistent descriptor set was built with
	Image* mpDecalAtlas = nullptr;
	Image* mpDecalNormalAtlas = nullptr;
};

} // namespace fx::renderer
