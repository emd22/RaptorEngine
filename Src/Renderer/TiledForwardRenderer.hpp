#pragma once

#include "Backend/BlendAttachment.hpp"
#include "Backend/Descriptors.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Shader.hpp"
#include "PipelineDesc.hpp"
#include "PipelineNames.hpp"
#include "RenderStage.hpp"
#include "Vertex.hpp"

#include <Object/ObjectLayer.hpp>

namespace fx {
class BaseCamera;
class Image;
} // namespace fx

namespace fx::renderer {

struct FrameData;
class CommandBuffer;


struct alignas(16) CompositionConsts
{
	uint32 FrameExtent[2];
};

struct alignas(16) SSAOConsts
{
	float32 InvProjection[16];
	float32 Projection[16];
	float32 View[16];
	float32 RenderSize[2];
	float32 EffectRadius;
	float32 FalloffRange;
	float32 SampleDistributionPower;
	float32 ThinOccluderCompensation;
	float32 FinalValuePower;
};

struct alignas(16) SSAOBlurConsts
{
	float32 TexelSize[2];
	float32 BlurBeta;
};

struct alignas(16) FogConsts
{
	float32 FarSunMatrix[16];
	float32 PrevCameraMatrix[16];
	float32 FarSunAtlasRect[4];
	float32 Rays[4][4];
	float32 OriginAndDensity[4];
	float32 CameraOffsetAndHaze[4];
	float32 AlbedoAndFalloff[4];
	float32 DepthA;
	float32 DepthB;
	float32 MarchDistance;
	float32 PreExposure;
	float32 Anisotropy;
	float32 AmbientScale;
	uint32 TileColumns;
	uint32 TileRows;
	uint32 Pad0;
	uint32 Flags;
	float32 JitterOffset;
	uint32 NoiseShift;
	float32 ScreenSize[2];
	float32 HistoryBlend;
	float32 JitterAmount;
	float32 BakedShadowRight[4];
	float32 BakedShadowUp[4];
	float32 BakedShadowForward[4];
	float32 BakedShadowGrid[4];
	float32 BakedShadowAtlas[4];
};

static constexpr uint32 scFroxelGridX = 160;
static constexpr uint32 scFroxelGridY = 90;
static constexpr uint32 scFroxelGridZ = 64;

enum class eFogFlags : uint32
{
	None = 0,
	ProbeSunVisibility = 0x01,
	FarSunShadow = 0x02,
	NearSunDynamicOnly = 0x04,
	BakedSunShadow = 0x08,
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

	void RenderComposition(BaseCamera& camera);

	/**
	 * @brief Dispatches the Forward+ light culling pass. Must be called outside of a renderpass,
	 * after all lights have been submitted for the frame.
	 * @param pExtentOverride Tile grid is derived from this extent instead of the swapchain
	 * (used by probe capture bakes, which render at a fixed small size).
	 * @param pLightOverride Culls these lights instead of every light submitted this frame.
	 */
	void DoLightCullingPass(BaseCamera& camera, const Vec2u* pExtentOverride = nullptr,
							const LightCullOverride* pLightOverride = nullptr);

	/**
	 * @brief Sets the atlases that decals are sampled from, rebuilding the persistent descriptor set when either
	 * changes. Null binds a blank image.
	 */
	void SetDecalAtlases(Image* atlas, Image* normal_atlas);

	void RebuildPersistentDescriptor();

	void Destroy();
	~TiledForwardRenderer() { Destroy(); }

private:
	// Geometry
	void CreateForwardPSO();
	void CreateDepthNormalPSO();
	void CreateSSAOPSO();
	void CreateSSAOBlurPSO();
	void CreateFogPSO();
	void CreateDebugLayerPSO();
	void CreateDebugSolidPSO();
	void CreateDebugOverlayPSO();

	void BuildPersistentDescriptor();

	void CreateForwardPass();
	void CreateDepthNormalPass();
	void CreateSSAOPass();
	void CreateSSAOBlurPass();
	void CreateParticlePass();
	void CreateFogPasses();
	Sampler* GetFogShadowSampler();

	// Lighting
	// void CreateLightVolumePipeline();
	void CreateLightingPipeline();
	void CreateLightingDSLayout();

	// Light culling
	void CreateLightCullingPSO();

	/// Registers the decal buffers and atlases (set 0) on the forward pipeline currently being built
	void AddDecalDescriptors();

	/// Set 0 of the forward pipelines
	void AddGlobalDescriptors();

	/**
	 * @brief The pass templates for the geometry pipelines, see PipelineCache::RegisterPassTemplate()
	 */
	bool MakeForwardDesc(ePipelinePass pass, ePipelineFeatures features, PipelineDesc& out_desc);
	bool MakePrepassDesc(ePipelineFeatures features, PipelineDesc& out_desc);

	// Composition
	void CreateCompositionPSO();
	void CreateBitmapTextPSO();
	void CreateParticlePSO();

	void GenerateRandomTexture(uint32 size);


public:
	DescriptorPool DescriptorPool;

	FX_FORCE_INLINE uint32 GetLightTileColumns() const { return mLightTileColumns; }
	FX_FORCE_INLINE uint32 GetLightTileRows() const { return mLightTileRows; }

	uint32 GetViewConstants(const BaseCamera& camera, eObjectLayer layer);

	/// Depth + Normal prepass
	RenderStage Prepass;
	/// Main Forward+ lit pass
	RenderStage ForwardPass;
	/// Particles over the lit target, sampling the prepass depth for soft edges
	RenderStage ParticlePass;
	/// SSAO pass
	RenderStage SSAOPass;
	/// SSAO blur pass
	RenderStage SSAODenoisePass;
	RenderStage SSAOBlurPass;
	/// Blends the integrated froxel fog over the lit target
	RenderStage FogApplyPass;
	/// Froxel fog: lighting per froxel, with one extra slice for the light past the grid
	Image FroxelLighting;
	/// Froxel fog: in-scatter accumulated from the eye to the far edge of each slice
	Image FroxelIntegrated;
	/// Froxel fog: last frame's FroxelLighting, reprojected into this frame's inject
	Image FroxelHistory;
	/// Froxel fog: per column nearest surface depth and the far end of the surface connected to it
	Image FroxelSurface;
	/// Composition pass, combine results from all passes
	RenderStage CompPass;


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

	struct CachedViewConstants
	{
		ViewConstants Consts;
		uint32 Offset = 0;
		uint32 Generation = UINT32_MAX;
	};

	CachedViewConstants mCachedViews[2];
};

} // namespace fx::renderer
