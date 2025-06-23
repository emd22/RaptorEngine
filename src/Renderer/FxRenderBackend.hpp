#pragma once

#include <deque>

#include "FxDeletionObject.hpp"
#include "FxWindow.hpp"

#include "Backend/RvkPipeline.hpp"
#include "Backend/RvkFrameData.hpp"
#include "Backend/RvkCommands.hpp"
#include "Backend/RvkSwapchain.hpp"
#include "Backend/RvkSynchro.hpp"

#include <Core/FxRef.hpp>

#include <ThirdParty/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct SDL_Window;

enum class FrameResult
{
    Success,
    GraphicsOutOfDate,
    RenderError,
};

struct FxGpuUploadContext
{
    RvkCommandPool CommandPool;
    RvkCommandBuffer CommandBuffer;

    RvkFence UploadFence;
};

class FxRenderBackend {
    const uint32 DeletionFrameSpacing = 3;

public:
    using SubmitFunc = std::function<void(RvkCommandBuffer& cmd)>;
public:
    FxRenderBackend() = default;

    using ExtensionList = FxSizedArray<VkExtensionProperties>;
    using ExtensionNames = std::vector<const char*>;

    void Init(Vec2u window_size);
    void Destroy();

    FrameResult BeginFrame(RvkGraphicsPipeline& pipeline, Mat4f& MVPMatrix);
    void FinishFrame(RvkGraphicsPipeline& pipeline);

    void SelectWindow(const FxRef<FxWindow>& window)
    {
        mWindow = window;
    }

    FxRef<FxWindow> GetWindow()
    {
        return mWindow;
    }

    RvkGpuDevice* GetDevice()
    {
        return &mDevice;
    }


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

    RvkFrameData *GetFrame();

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


    uint32 GetFrameNumber() { return mFrameNumber; }

private:
    void InitVulkan();
    void CreateSurfaceFromWindow();

    void InitGPUAllocator();
    void DestroyGPUAllocator();

    void InitUploadContext();
    void DestroyUploadContext();

    void SubmitFrame();
    void PresentFrame();

    void InitFrames();
    void DestroyFrames();

    FrameResult GetNextSwapchainImage(RvkFrameData* frame);

    ExtensionList &QueryInstanceExtensions(bool invalidate_previous = false);
    ExtensionNames MakeInstanceExtensionList(ExtensionNames& user_requested_extensions);
    ExtensionNames CheckExtensionsAvailable(ExtensionNames& requested_extensions);

    FxSizedArray<VkLayerProperties> GetAvailableValidationLayers();

public:
    RvkSwapchain Swapchain;
    FxSizedArray<RvkFrameData> Frames;

    VmaAllocator GpuAllocator = nullptr;

    // XXX: temporary
    RvkDescriptorPool DescriptorPool;

    FxGpuUploadContext UploadContext;

    bool Initialized = false;

private:

    VkInstance mInstance = nullptr;
    VkSurfaceKHR mWindowSurface = nullptr;

    FxRef<FxWindow> mWindow = nullptr;
    RvkGpuDevice mDevice;

    VkDebugUtilsMessengerEXT mDebugMessenger;

    ExtensionList mAvailableExtensions;

    uint32 mImageIndex = 0;

protected:
    uint32 mFrameNumber = 0;
    uint32 mInternalFrameCounter = 0;

    std::atomic_bool mInDeletionQueue = false;
    std::deque<FxDeletionObject> mDeletionQueue;
};
