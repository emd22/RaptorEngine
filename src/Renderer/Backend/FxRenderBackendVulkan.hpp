#pragma once

#include <Renderer/FxRenderBackend.hpp>

#include "ThirdParty/vk_mem_alloc.h"
#include "vulkan/vulkan_core.h"

#include "Vulkan/Swapchain.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/FrameData.hpp"

#include <Renderer/FxWindow.hpp>

#include <memory>
#include <vulkan/vulkan.hpp>


struct SDL_Window;

namespace vulkan {

class FxRenderBackendVulkan final : public FxRenderBackend {
public:
    FxRenderBackendVulkan() = default;

    using ExtensionList = StaticArray<VkExtensionProperties>;
    using ExtensionNames = std::vector<const char *>;

    void Init(Vec2i window_size) override;
    void Destroy() override;

    FrameResult BeginFrame(GraphicsPipeline &pipeline) override;
    void FinishFrame(GraphicsPipeline &pipeline) override;

    void SelectWindow(std::shared_ptr<FxWindow> window) override
    {
        mWindow = window;
    }

    std::shared_ptr<FxWindow> GetWindow() override
    {
        return mWindow;
    }

    vulkan::GPUDevice *GetDevice()
    {
        return &mDevice;
    }

    FrameData *GetFrame();

    uint32 GetImageIndex() { return mImageIndex; }
    VmaAllocator *GetGPUAllocator() { return &GPUAllocator; }

    ~FxRenderBackendVulkan()
    {
        Destroy();
    }

private:
    void InitVulkan();
    void CreateSurfaceFromWindow();

    void InitGPUAllocator();
    void DestroyGPUAllocator();

    void SubmitFrame();
    void PresentFrame();

    void InitFrames();
    void DestroyFrames();

    FrameResult GetNextSwapchainImage(FrameData *frame);

    ExtensionList &QueryInstanceExtensions(bool invalidate_previous = false);
    ExtensionNames MakeInstanceExtensionList(ExtensionNames &user_requested_extensions);
    ExtensionNames CheckExtensionsAvailable(ExtensionNames &requested_extensions);

    StaticArray<VkLayerProperties> GetAvailableValidationLayers();

public:
    vulkan::Swapchain Swapchain;
    StaticArray<FrameData> Frames;

    VmaAllocator GPUAllocator = nullptr;
private:

    VkInstance mInstance = nullptr;
    VkSurfaceKHR mWindowSurface = nullptr;

    std::shared_ptr<FxWindow> mWindow = nullptr;
    vulkan::GPUDevice mDevice;

    VkDebugUtilsMessengerEXT mDebugMessenger;

    ExtensionList mAvailableExtensions;

    uint32 mImageIndex = 0;
};

}; // namespace vulkan
