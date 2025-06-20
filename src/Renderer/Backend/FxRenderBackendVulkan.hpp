#pragma once

#include <Renderer/FxRenderBackend.hpp>

#include <memory>

#define VMA_DEBUG_LOG_FORMAT(str, ...) \
    Log::Warning(str, __VA_ARGS__);

#include "ThirdParty/vk_mem_alloc.h"
#include <vulkan/vulkan.h>

#include "Vulkan/RvkSwapchain.hpp"
#include "Vulkan/RvkDevice.hpp"
#include "Vulkan/RvkFrameData.hpp"
#include "Vulkan/RvkCommands.hpp"

#include "Vulkan/RvkDescriptors.hpp"

#include <Renderer/FxWindow.hpp>

#include <Renderer/Backend/Vulkan/RvkSynchro.hpp>

#include <Core/FxRef.hpp>

struct SDL_Window;

namespace vulkan {

struct FxGpuUploadContext
{
    RvkCommandPool CommandPool;
    RvkCommandBuffer CommandBuffer;

    RvkFence UploadFence;
};

class FxRenderBackendVulkan final : public FxRenderBackend {
public:
    using SubmitFunc = std::function<void(RvkCommandBuffer &cmd)>;
public:
    FxRenderBackendVulkan() = default;

    using ExtensionList = FxStaticArray<VkExtensionProperties>;
    using ExtensionNames = std::vector<const char *>;

    void Init(Vec2u window_size) override;
    void Destroy() override;

    FrameResult BeginFrame(RvkGraphicsPipeline &pipeline, Mat4f &MVPMatrix) override;
    void FinishFrame(RvkGraphicsPipeline &pipeline) override;

    void SelectWindow(FxRef<FxWindow> window) override
    {
        mWindow = window;
    }

    FxRef<FxWindow> GetWindow() override
    {
        return mWindow;
    }

    vulkan::RvkGpuDevice* GetDevice()
    {
        return &mDevice;
    }

    RvkFrameData *GetFrame();

    uint32 GetImageIndex() { return mImageIndex; }
    VmaAllocator *GetGPUAllocator() { return &GpuAllocator; }

    void SubmitUploadCmd(SubmitFunc func);
    void SubmitOneTimeCmd(SubmitFunc func);

    ~FxRenderBackendVulkan()
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

    FxStaticArray<VkLayerProperties> GetAvailableValidationLayers();

public:
    vulkan::RvkSwapchain Swapchain;
    FxStaticArray<RvkFrameData> Frames;

    VmaAllocator GpuAllocator = nullptr;

    // XXX: temporary
    RvkDescriptorPool DescriptorPool;

    FxGpuUploadContext UploadContext;
private:

    VkInstance mInstance = nullptr;
    VkSurfaceKHR mWindowSurface = nullptr;

    FxRef<FxWindow> mWindow = nullptr;
    vulkan::RvkGpuDevice mDevice;

    VkDebugUtilsMessengerEXT mDebugMessenger;

    ExtensionList mAvailableExtensions;

    uint32 mImageIndex = 0;
};

}; // namespace vulkan
