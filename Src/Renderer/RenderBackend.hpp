#pragma once

#include "Backend/Commands.hpp"
#include "Backend/FrameData.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Swapchain.hpp"
#include "Backend/Synchro.hpp"
#include "DeferredRenderer.hpp"
#include "DeletionObject.hpp"
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

class DeferredRenderer;
class DeferredGPass;
class DeferredCompPass;

struct GpuUploadContext
{
    CommandPool CmdPool;
    CommandBuffer CmdBuffer;
    CommandBuffer ImmediateCmdBuffer;

    Fence UploadFence;
    Fence ImmediateUploadFence;

    ~GpuUploadContext() = default;
};


class RenderBackend
{
    const uint32 scDeletionFrameSpacing = 3;

    static constexpr uint32 scLightUniformSize = 240;

public:
    using SubmitFunc = std::function<void(CommandBuffer& cmd)>;

public:
    RenderBackend() = default;

    using ExtensionList = SizedArray<VkExtensionProperties>;
    using ExtensionNames = std::vector<const char*>;

    void Init(Vec2u window_size);
    void Destroy();

    eFrameResult BeginFrame();
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

    ~RenderBackend() { Destroy(); }

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

    bool RequiresVulkanPortability();

    eFrameResult GetNextSwapchainImage(FrameData* frame);

    ExtensionList& QueryInstanceExtensions(bool invalidate_previous = false);
    ExtensionNames MakeInstanceExtensionList(ExtensionNames& user_requested_extensions);
    ExtensionNames CheckExtensionsAvailable(ExtensionNames& requested_extensions);

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

    DeferredCompPass* pCurrentCompPass = nullptr;
    DeferredLightingPass* pCurrentLightingPass = nullptr;

    Ref<DeferredRenderer> pDeferredRenderer { nullptr };

    Uniforms LightBuffer;
    Uniforms BoneBuffer;

private:
    VkInstance mInstance = nullptr;
    VkSurfaceKHR mWindowSurface = nullptr;

    Ref<Window> mpWindow = nullptr;
    GpuDevice mDevice;

    VkDebugUtilsMessengerEXT mDebugMessenger;

    ExtensionList mAvailableExtensions;

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
