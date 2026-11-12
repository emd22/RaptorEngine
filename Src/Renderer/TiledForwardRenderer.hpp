#pragma once

#include "Backend/BlendAttachment.hpp"
#include "Backend/Descriptors.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Shader.hpp"
#include "PipelineDesc.hpp"
#include "PipelineNames.hpp"
#include "RenderStage.hpp"
#include "Vertex.hpp"

#include <Math/FMat4.hpp>
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
	FarSunShadow = 0x02,
	NearSunDynamicOnly = 0x04,
	BakedSunShadow = 0x08,

	// r_fog_debug bits, shifted up by scFogDebugFlagShift
	DebugNoClamp = 0x10,
	DebugAcceptAll = 0x20,
	DebugSlopeAcceptance = 0x40,
};

static constexpr uint32 scFogDebugFlagShift = 4;
static constexpr uint32 scFogDebugFlagMask = 0x7;


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
	void Create(const UVec2& extent);

	void RenderComposition(BaseCamera& camera);

	/**
	 * @brief Dispatches the Forward+ light culling pass. Must be called outside of a renderpass,
	 * after all lights have been submitted for the frame.
	 * @param pExtentOverride Tile grid is derived from this extent instead of the swapchain
	 * (used by probe capture bakes, which render at a fixed small size).
	 * @param pLightOverride Culls these lights instead of every light submitted this frame.
	 */
	void DoLightCullingPass(BaseCamera& camera, const UVec2* pExtentOverride = nullptr,
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
	/// The half resolution gather of the GI probes for the main view, which the forward pass reads back
	void CreateGIResolvePSO();
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
	void BuildLightCullingPipeline(ePipelineName name, bool depth_bounds);

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
	FX_FORCE_INLINE uint32 GetLightTileColumns() const { return mLightTileColumns; }
	FX_FORCE_INLINE uint32 GetLightTileRows() const { return mLightTileRows; }

	uint32 GetViewConstants(const BaseCamera& camera, eObjectLayer layer);

	/// Remembers last frame's matrices of the main view, which the view constants pass on for motion vectors
	void BeginViewMotion(const BaseCamera& camera);

public:
	DescriptorPool DescriptorPool;


	/// Depth + Normal prepass
	RenderStage Prepass;

	/// Main Forward+ lit pass
	RenderStage ForwardPass;

	/* Water + Blending */
	/// Copies of the opaque lit target and depth, pre any sort of blending passes
	RenderStage SceneCopyPass;
	/// Continues ForwardPass on the same targets once the scene has been copied
	RenderStage ForwardBlendPass;

	RenderStage ParticlePass;

	/* SSAO */
	RenderStage SSAOPass;
	RenderStage SSAODenoisePass;
	RenderStage SSAOBlurPass;

	/// The GI probes gathered at half resolution: diffuse + eye distance, then the packed specular and normal. Two,
	/// used in turn: each frame's resolve writes one and reprojects the other, last frame's, so nothing is copied.
	/// GIResolveIndex is the one the forward pass reads, the last written.
	RenderStage GIResolvePasses[2];
	uint32 GIResolveIndex = 0;

	/* Volumetric Lighting */
	RenderStage FogApplyPass;
	Image FroxelLighting;
	Image FroxelIntegrated;
	Image FroxelHistory;
	Image FroxelSurface;

	/* Composition, combines results from previous passes */
	RenderStage CompPass;


	/// Descriptors that remain bound for the entirety of the frame. This includes object buffer, material buffer, etc.
	/// One of PersistentDescriptors: the one whose GI resolve images are GIResolvePasses[GIResolveIndex].
	DescriptorSet* pPersistentDescriptor = nullptr;
	DescriptorSet* PersistentDescriptors[2] = { nullptr, nullptr };

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

	const BaseCamera* mpMotionCamera = nullptr;
	FMat4 mMotionViewProjection[2] = { FMat4::scIdentity, FMat4::scIdentity };
	FMat4 mMotionPrevViewProjection[2] = { FMat4::scIdentity, FMat4::scIdentity };
};

} // namespace fx::renderer
