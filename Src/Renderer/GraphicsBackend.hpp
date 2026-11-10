#pragma once

#include "Backend/Commands.hpp"
#include "Backend/FrameData.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Swapchain.hpp"
#include "Backend/Synchro.hpp"
#include "DeletionObject.hpp"
#include "FrameAllocator.hpp"
#include "GpuProfiler.hpp"
#include "LightTransport.hpp"
#include "ProbeClipmap.hpp"
#include "SDFPageScene.hpp"
#include "TiledForwardRenderer.hpp"
#include "UniformBuffer.hpp"
#include "Window.hpp"

#include <ThirdParty/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <Core/Defer.hpp>
#include <Core/Ref.hpp>
#include <Core/TSQueue.hpp>
#include <Math/Vec2.hpp>
#include <mutex>
#include <string>

namespace fx {
class BaseCamera;
class PerspectiveCamera;
class Image;
struct CompiledMapFogShadow;
} // namespace fx

namespace fx::renderer {


enum class eFrameResult
{
	Success,
	GraphicsOutOfDate,
	RenderError,
};

enum class eFogMode : int64
{
	Off = 0,
	Froxel = 1,
};

class TiledForwardRenderer;

/**
 * @brief Where the fog shadow pages of the loaded compiled map are, see CompiledMapFogShadow
 */
struct BakedFogShadowParams
{
	float32 Right[3] = {};
	float32 Up[3] = {};
	float32 Forward[3] = {};

	float32 PageSize = 0.0f;

	int32 CellMin[2] = {};
	uint32 CellCount[2] = {};

	float32 InvAtlasSize[2] = {};
};

/// The baked sun shadow lookup as BakedSunShadowParams in Shaders/FogConsts.hlsli lays it out
struct BakedSunShadowGpu
{
	/// XYZ are the light space axes. W: page size, depth bias, PCF radius.
	float32 Right[4] = {};
	float32 Up[4] = {};
	float32 Forward[4] = {};
	/// First cell XY, cell counts ZW
	float32 Grid[4] = {};
	/// Reciprocal atlas size in XY
	float32 Atlas[4] = {};
};

/// A light written into the light buffer this frame, for picking the lights the GI shades its ray hits with
struct ShadeLightCandidate
{
	uint32 Slot = 0;
	bool bDirectional = false;
	FVec3 Position = FVec3::sZero;
	float32 Radius = 0.0f;
};

struct GpuUploadContext
{
	CommandPool CmdPool;
	CommandBuffer CmdBuffer;
	Fence UploadFence;

	CommandPool ImmediateCmdPool;
	CommandBuffer ImmediateCmdBuffer;
	Fence ImmediateUploadFence;
	std::mutex ImmediateMutex;
	std::atomic_uint32_t ImmediateUploadsInFlight = 0;

	~GpuUploadContext() = default;
};


class GraphicsBackend
{
	const uint32 scDeletionFrameSpacing = 3;

	static constexpr uint32 scLightUniformSize = sizeof(LightGpuData);

public:
	using SubmitFunc = std::function<void(CommandBuffer& cmd)>;

public:
	GraphicsBackend() = default;

	using ExtensionList = SizedArray<VkExtensionProperties>;
	using ExtensionNames = std::vector<const char*>;

	void Init(UVec2 window_size);
	void Destroy();

	eFrameResult BeginFrame();
	void BeginLightCulling(BaseCamera& render_cam);
	void BeginPrepass();
	void BeginGeometry();

	void CopySceneForWater();

	void RenderEarlyFrameEffects(PerspectiveCamera& camera);

	void RenderFog(PerspectiveCamera& camera);
	void RenderFroxelFog(CommandBuffer& cmd, uint32 consts_offset);

	eFogMode GetFogMode() const;

	void DoComposition(PerspectiveCamera& render_cam);

	void RebuildToResizedWindow();

	void SelectWindow(const Ref<Window>& window) { mpWindow = window; }

	FX_FORCE_INLINE Ref<Window> GetWindow() { return mpWindow; }
	FX_FORCE_INLINE GpuDevice* GetDevice() { return &mDevice; }

	VkInstance GetVulkanInstance() { return mInstance; }

	FrameData* GetFrame();

	uint32 GetImageIndex() const { return mImageIndex; }
	VmaAllocator* GetGPUAllocator() { return &GpuAllocator; }

	template <typename T>
	void SubmitPushConstants(const CommandBuffer& cmd, const Pipeline& pipeline, eShaderType shader_types,
							 const T& value) const
	{
		SubmitPushConstantsRaw(cmd, pipeline, shader_types, &value, sizeof(T));
	}


	void BeginUploads();
	void SubmitUploads();

	void SubmitUploadCmd(SubmitFunc func);
	void SubmitImmediateUploadCmd(SubmitFunc func);
	void SubmitOneTimeCmd(SubmitFunc func);

	~GraphicsBackend() { Destroy(); }

	bool ProcessDeletionQueue(bool immediate, Queue<DeletionObject>& deletion_queue);

	uint32 GetElapsedFrameCount() const { return mInternalFrameCounter.load(); }

	void CountFrameWithoutDrawing() { mInternalFrameCounter.fetch_add(1); }

	void RequestFrameCapture(const char* path);
	uint32 GetFrameNumber() const { return mFrameNumber; }

	FX_FORCE_INLINE bool DidResize() const { return bDidFrameResize; }


	void MarkGpu(eGpuMarker marker) { Profiler.Mark(GetFrame()->CmdBuffer, marker); }


	bool UploadLightmap(const UVec2& size, const uint8* sun_visibility, const uint16* irradiance,
						const uint8* directions);
	Image* CreateLightmapImage(const UVec2& size, eImageFormat format, const Slice<const uint8>& texels);

	bool UploadFogShadow(const CompiledMapFogShadow& shadow);
	void ClearFogShadow() { bHasBakedFogShadow = false; }

	/// True when the baked sun visibility is current and the map has fog shadow pages to look it up in
	bool IsBakedSunShadowActive() const { return bBakedSunActive && bHasBakedFogShadow; }
	BakedSunShadowGpu GetBakedSunShadowGpu() const;

	/**
	 * @brief The light buffer slots the GI shades its hits with: the sun, then the nearest local lights that reach
	 * within `reach` of `center`, up to SDFPageScene::scShadeMaxLights
	 */
	void SelectShadeLights(const FVec3& center, float32 reach, uint32 out_slots[4], uint32& out_count) const;

	bool HasDebugView() const { return bOnlyRenderProbes || bRenderProbeVisibility || (ReflectionDebugView != 0); }


	FX_FORCE_INLINE uint32 GetLightGridFrameOffset() const { return LightGridPageSize * GetFrameNumber(); }
	FX_FORCE_INLINE uint32 GetLightIndexListFrameOffset() const { return LightIndexListPageSize * GetFrameNumber(); }


	FX_FORCE_INLINE uint32 GetDecalFrameOffset() const { return DecalPageSize * GetFrameNumber(); }
	FX_FORCE_INLINE uint32 GetDecalMaskFrameOffset() const { return DecalMaskPageSize * GetFrameNumber(); }

	Image* GetShadingIrradiance() const
	{
		Image* relit = Transport.GetRelitIrradiance();

		if (relit != nullptr) {
			return relit;
		}

		return pLightmapIrradiance;
	}

private:
	void RecordFrameCapture(CommandBuffer& cmd);
	void WriteFrameCapture();


	uint64 mFrameCaptureBytes = 0;
	uint32 mFrameCaptureWidth = 0;
	uint32 mFrameCaptureHeight = 0;

	void InitVulkan();
	void CreateSurfaceFromWindow();

	void InitGPUAllocator();
	void DestroyGPUAllocator();

	void InitUploadContext();
	void DestroyUploadContext();

	void PresentFrame();

	void InitFrames();
	void DestroyFrames();

	void RebuildRenderStages();

	bool RequiresVulkanPortability(const ExtensionList& available_extensions);

	eFrameResult GetNextSwapchainImage(FrameData* frame);

	ExtensionList& QueryInstanceExtensions(ExtensionList& available_extensions, bool invalidate_previous = false);
	ExtensionNames MakeInstanceExtensionList(ExtensionNames& user_requested_extensions,
											 ExtensionList& out_available_extensions);
	ExtensionNames CheckExtensionsAvailable(ExtensionNames& requested_extensions, ExtensionList& available_extensions);

	void SubmitPushConstantsRaw(const CommandBuffer& cmd, const Pipeline& pipeline, eShaderType shader_types,
								const void* data, uint32 data_size) const;

	SizedArray<VkLayerProperties> GetAvailableValidationLayers();


public:
	Swapchain Swapchain;
	SizedArray<FrameData> Frames;

	VmaAllocator GpuAllocator = nullptr;

	GpuUploadContext UploadContext;

	bool bInitialized = false;
	bool bDidFrameResize = false;

	float32 DeltaTime = 1.0f / 60.0f;

	TiledForwardRenderer* pRenderer { nullptr };

	Uniforms LightBuffer;

	/// Skinning matrices for every skinned object posed this frame, one slot per matrix. Bound as a storage buffer, as
	/// large skeletons quickly go past the 64KB range a uniform buffer is limited to.
	Uniforms BoneBuffer;

	FrameAllocator FrameAlloc;

	///////////////////////////////////
	// Forward+ Tiled Lighting
	///////////////////////////////////

	RawGpuBuffer LightGridBuffer;
	RawGpuBuffer LightIndexListBuffer;

	uint32 LightGridPageSize = 0;
	uint32 LightIndexListPageSize = 0;

	///////////////////////////////////
	// Clustered Decals
	///////////////////////////////////

	RawGpuBuffer DecalBuffer;
	RawGpuBuffer DecalMaskBuffer;

	uint32 DecalPageSize = 0;
	uint32 DecalMaskPageSize = 0;

	///////////////////////////////////
	// Light Probes
	///////////////////////////////////

	RawGpuBuffer ProbeBuffer;
	uint32 ProbePageSize = 0;

	RawGpuBuffer ProbeVolumeBuffer;
	uint32 ProbeVolumePageSize = 0;

	RawGpuBuffer ProbeGridBuffer;
	uint32 ProbeGridPageSize = 0;

	Image* pProbeMomentsAtlas = nullptr;

	RawGpuBuffer ReflectionProbeBuffer;
	uint32 ReflectionProbePageSize = 0;

	Image* pReflectionProbes = nullptr;

	Semaphore TransferSync;
	std::atomic_uint64_t TransferCount = 0;

	DescriptorSet* pLightsDescriptor = nullptr;

	Image* pNoiseTexture = nullptr;

	Image* pDfgLut = nullptr;

	Image* pLightmap = nullptr;
	Image* pLightmapIrradiance = nullptr;

	LightTransport Transport;

	SDFPageScene SDFPages;
	SDFPageScene::eDebugView SDFPageDebugView = SDFPageScene::eDebugView::None;
	float32 SDFPageDebugCone = 0.0f;
	float32 SDFPageDebugRayLength = 50.0f;
	/// Only every this many probes trace in the probe ray views
	uint32 SDFPageDebugProbeStride = 1;

	ProbeClipmap GIClipmap;
	ProbeClipmap::eDebugView GIClipmapDebugView = ProbeClipmap::eDebugView::None;

	/// Filled by the world as it writes the light buffer each frame
	SizedArray<ShadeLightCandidate> ShadeLights;

	Image* pFogShadowAtlas = nullptr;

	RawGpuBuffer FogShadowPageBuffer;
	uint32 FogShadowPageBufferSize = 0;
	BakedFogShadowParams FogShadow;
	bool bHasBakedFogShadow = false;

	bool bBakedSunActive = false;
	bool bBakedIndirectActive = false;
	bool bFarSunShadowActive = false;


	bool bDisableProbes = false;
	bool bDisableReflectionProbes = false;
	bool bDisableReflectionNormalize = false;
	uint32 ReflectionDebugView = 0;
	bool bDisableDecals = false;

	float32 PreExposure = 1.0f;

	/// The number of seconds the graphics backend has been running for.
	float64 TotalRunTime = 0.0;

	uint32 Tonemapper = 1;

	bool bOnlyRenderProbes = false;
	bool bRenderProbeVisibility = false;

	GpuProfiler Profiler;


private:
	GpuDevice mDevice;
	VkInstance mInstance = nullptr;
	VkDebugUtilsMessengerEXT mDebugMessenger;

	VkSurfaceKHR mWindowSurface = nullptr;
	Ref<Window> mpWindow = nullptr;

	SizedArray<Semaphore> mSubmitSemaphores;

	uint32 mImageIndex = 0;

	std::string mFrameCapturePath;
	RawGpuBuffer mFrameCaptureBuffer;

protected:
	uint32 mFrameNumber = 0;
	std::atomic_uint32_t mInternalFrameCounter = 0;

	FMat4 mFogPrevCameraMatrix = FMat4::scIdentity;
	uint32 mFroxelHistoryFrame = UINT32_MAX;

	/// The stage the geometry is being drawn into, ForwardPass or ForwardBlendPass after CopySceneForWater()
	RenderStage* mpActiveForwardStage = nullptr;


	// TSQueue<DeletionObject> mDeletionQueue;
};

} // namespace fx::renderer
