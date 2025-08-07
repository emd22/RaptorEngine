#pragma once

#include <deque>

#include "FxDeletionObject.hpp"
#include "FxWindow.hpp"

#include "Backend/RxPipeline.hpp"
#include "Backend/RxFrameData.hpp"
#include "Backend/RxCommands.hpp"
#include "Backend/RxSwapchain.hpp"
#include "Backend/RxSynchro.hpp"

#include <Core/FxRef.hpp>

#include "FxDeferred.hpp"

#include <ThirdParty/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct SDL_Window;

enum class FrameResult
{
    Success,
    GraphicsOutOfDate,
    RenderError,
};

class FxDeferredRenderer;
class FxDeferredGPass;
class FxDeferredCompPass;

struct FxGpuUploadContext
{
    RxCommandPool CommandPool;
    RxCommandBuffer CommandBuffer;

    RxFence UploadFence;
};

class FxCamera;

class FxRenderBackend {
    const uint32 DeletionFrameSpacing = 3;

public:
    using SubmitFunc = std::function<void(RxCommandBuffer& cmd)>;
public:
    FxRenderBackend() = default;

    using ExtensionList = FxSizedArray<VkExtensionProperties>;
    using ExtensionNames = std::vector<const char*>;

    void Init(FxVec2u window_size);
    void Destroy();

    FrameResult BeginFrame(FxDeferredRenderer& renderer);
    void BeginLighting();
    void DoComposition(FxCamera& render_cam);

    void SelectWindow(const FxRef<FxWindow>& window)
    {
        mWindow = window;
    }

    FX_FORCE_INLINE FxRef<FxWindow> GetWindow()
    {
        return mWindow;
    }

    FX_FORCE_INLINE RxGpuDevice* GetDevice()
    {
        return &mDevice;
    }

    RxUniformBufferObject& GetUbo();

    void AddGpuBufferToDeletionQueue(VkBuffer buffer, VmaAllocation allocation)
    {
        // Log::Info("Adding GPUBuffer to deletion queue at frame %d", mInternalFrameCounter);
        if (mInDeletionQueue) {
            mInDeletionQueue.wait(false);
        }

        mInDeletionQueue.store(true);

        mDeletionQueue.push_back(FxDeletionObject {
            .DeletionFrameNumber = mInternalFrameCounter + DeletionFrameSpacing,
            .IsGpuBuffer = true,
            .Allocation = allocation,
            .Buffer = buffer,
        });

        mInDeletionQueue.store(false);
    }

    VkInstance GetVulkanInstance()
    {
        return mInstance;
    }

    RxFrameData *GetFrame();

    uint32 GetImageIndex() { return mImageIndex; }
    VmaAllocator *GetGPUAllocator() { return &GpuAllocator; }

    void SubmitUploadCmd(SubmitFunc func);
    void SubmitOneTimeCmd(SubmitFunc func);

    ~FxRenderBackend()
    {
        Destroy();
    }

    void ProcessDeletionQueue(bool immediate = false)
    {
        if (mDeletionQueue.empty()) {
            return;
        }

        if (immediate && mInDeletionQueue) {
            mInDeletionQueue.wait(false);
        }
        if (mInDeletionQueue) {
            return;
        }

        mInDeletionQueue.store(true);

        FxDeletionObject& object = mDeletionQueue.front();

        // Log::Debug("Deleting object from deletion queue from frame %d", object.DeletionFrameNumber);

        const bool is_frame_spaced = (mInternalFrameCounter >= object.DeletionFrameNumber);

        if (immediate || is_frame_spaced) {
            if (object.IsGpuBuffer) {
                vmaDestroyBuffer(GpuAllocator, object.Buffer, object.Allocation);
            }
            else {
                object.Func(&object);
            }

            mDeletionQueue.pop_front();
        }

        mInDeletionQueue.store(false);
    }

    void AddToDeletionQueue(FxDeletionObject::FuncType func)
    {
        Log::Info("Adding object to deletion queue at frame %d", mInternalFrameCounter);

        mDeletionQueue.push_back(FxDeletionObject {
            .DeletionFrameNumber = mInternalFrameCounter,
            .Func = func,
        });
    }

    uint32 GetElapsedFrameCount() const { return mInternalFrameCounter; }
    uint32 GetFrameNumber() const { return mFrameNumber; }

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

    FrameResult GetNextSwapchainImage(RxFrameData* frame);

    ExtensionList &QueryInstanceExtensions(bool invalidate_previous = false);
    ExtensionNames MakeInstanceExtensionList(ExtensionNames& user_requested_extensions);
    ExtensionNames CheckExtensionsAvailable(ExtensionNames& requested_extensions);

    FxSizedArray<VkLayerProperties> GetAvailableValidationLayers();

public:
    RxSwapchain Swapchain;
    FxSizedArray<RxFrameData> Frames;

    VmaAllocator GpuAllocator = nullptr;

    // XXX: temporary
    // RxDescriptorPool GPassDescriptorPool;
    RxDescriptorPool CompDescriptorPool;

    FxGpuUploadContext UploadContext;

    bool Initialized = false;

    FxDeferredGPass* CurrentGPass = nullptr;
    FxDeferredCompPass* CurrentCompPass = nullptr;
    FxDeferredLightingPass* CurrentLightingPass = nullptr;

    FxRef<FxDeferredRenderer> DeferredRenderer{ nullptr };

    // RxSemaphore OffscreenSemaphore;

private:

    VkInstance mInstance = nullptr;
    VkSurfaceKHR mWindowSurface = nullptr;

    FxRef<FxWindow> mWindow = nullptr;
    RxGpuDevice mDevice;

    VkDebugUtilsMessengerEXT mDebugMessenger;

    ExtensionList mAvailableExtensions;


    uint32 mImageIndex = 0;

protected:
    uint32 mFrameNumber = 0;
    uint32 mInternalFrameCounter = 0;

    std::atomic_bool mInDeletionQueue = false;
    std::deque<FxDeletionObject> mDeletionQueue;
};
