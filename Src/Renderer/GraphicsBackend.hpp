#pragma once

#include "Backend/Commands.hpp"
#include "Backend/FrameData.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Swapchain.hpp"
#include "Backend/Synchro.hpp"
#include "DeletionObject.hpp"
#include "TiledForwardRenderer.hpp"
#include "UniformBuffer.hpp"
#include "Window.hpp"

#include <ThirdParty/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <Core/Defer.hpp>
#include <Core/Ref.hpp>
#include <Core/TSQueue.hpp>
// #include <deque>
// #include <mutex>

namespace fx {
class Camera;
}

namespace fx::renderer {

enum class eFrameResult
{
	Success,
	GraphicsOutOfDate,
	RenderError,
};

class TiledForwardRenderer;

struct GpuUploadContext
{
	CommandPool CmdPool;
	CommandBuffer CmdBuffer;
	Fence UploadFence;

	CommandBuffer ImmediateCmdBuffer;
	Fence ImmediateUploadFence;

	~GpuUploadContext() = default;
};


class GraphicsBackend
{
	const uint32 scDeletionFrameSpacing = 3;

	static constexpr uint32 scLightUniformSize = 240;

public:
	using SubmitFunc = std::function<void(CommandBuffer& cmd)>;

public:
	GraphicsBackend() = default;

	using ExtensionList = SizedArray<VkExtensionProperties>;
	using ExtensionNames = std::vector<const char*>;

	void Init(Vec2u window_size);
	void Destroy();

	eFrameResult BeginFrame();
	void BeginLightCulling(Camera& render_cam);
	void BeginGeometry();
	void BeginLighting();
	void BeginUnlit();
	void DoComposition(Camera& render_cam);

	void RebuildToResizedWindow();

	void SelectWindow(const Ref<Window>& window) { mpWindow = window; }

	FX_FORCE_INLINE Ref<Window> GetWindow() { return mpWindow; }
	FX_FORCE_INLINE GpuDevice* GetDevice() { return &mDevice; }

	void AddGpuBufferToDeletionQueue(VkBuffer buffer, VmaAllocation allocation)
	{
		SpinLockContext<Queue<DeletionObject>> deletion_queue = mDeletionQueue.GetQueue();

		DeletionObject obj = {
			.Buffer = buffer,
			.Allocation = allocation,
			.DeletionFrameNumber = mInternalFrameCounter + scDeletionFrameSpacing,
			.bIsGpuBuffer = true,
		};

		deletion_queue->Push(std::move(obj));
	}

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

	bool ProcessDeletionQueue(bool immediate, Queue<DeletionObject>& deletion_queue)
	{
		// if (immediate) {
		//     mInDeletionQueue.lock();
		// }
		// else if (!mInDeletionQueue.try_lock()) {
		//     return false;
		// }

		if (deletion_queue.IsEmpty()) {
			return false;
		}

		DeletionObject& object = deletion_queue.First();

		const bool is_frame_spaced = (mInternalFrameCounter >= object.DeletionFrameNumber);

		bool did_delete = false;

		if (immediate || is_frame_spaced) {
			if (object.bIsGpuBuffer) {
				vmaDestroyBuffer(GpuAllocator, object.Buffer, object.Allocation);
			}
			else {
				object.Func(&object);
			}

			did_delete = true;
			deletion_queue.Pop();
		}

		return did_delete;
	}

	void AddToDeletionQueue(DeletionObject::FuncType func)
	{
		SpinLockContext<Queue<DeletionObject>> deletion_queue = mDeletionQueue.GetQueue();

		DeletionObject obj = {
			.DeletionFrameNumber = mInternalFrameCounter + scDeletionFrameSpacing,
			.Func = func,
		};

		deletion_queue->Push(std::move(obj));
	}

	uint32 GetElapsedFrameCount() const { return mInternalFrameCounter.load(); }
	uint32 GetFrameNumber() const { return mFrameNumber; }

	FX_FORCE_INLINE bool DidResize() const { return bDidFrameResize; }

private:
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

	TiledForwardRenderer* pRenderer { nullptr };

	Uniforms LightBuffer;
	Uniforms BoneBuffer;

	///////////////////////////////////
	// Forward+ Tiled Lighting
	///////////////////////////////////

	/// Per tile light list offsets, indexed by tile. Written by the light culling compute pass.
	RawGpuBuffer LightGridBuffer;

	/// Global list of light indices, each tile owns a fixed region of `MaxLightsPerTile` entries.
	RawGpuBuffer LightIndexListBuffer;

	uint32 LightGridPageSize = 0;
	uint32 LightIndexListPageSize = 0;

	FX_FORCE_INLINE uint32 GetLightGridFrameOffset() const { return LightGridPageSize * GetFrameNumber(); }
	FX_FORCE_INLINE uint32 GetLightIndexListFrameOffset() const { return LightIndexListPageSize * GetFrameNumber(); }

	Semaphore TransferSync;
	std::atomic_uint64_t TransferCount = 0;

	DescriptorSet* pLightsDescriptor = nullptr;

private:
	GpuDevice mDevice;
	VkInstance mInstance = nullptr;
	VkDebugUtilsMessengerEXT mDebugMessenger;

	VkSurfaceKHR mWindowSurface = nullptr;
	Ref<Window> mpWindow = nullptr;

	SizedArray<Semaphore> mSubmitSemaphores;

	uint32 mImageIndex = 0;


protected:
	uint32 mFrameNumber = 0;
	std::atomic_uint32_t mInternalFrameCounter = 0;

	TSQueue<DeletionObject> mDeletionQueue;
	// std::mutex mInDeletionQueue;
	// std::deque<DeletionObject> mDeletionQueue;
};

} // namespace fx::renderer
