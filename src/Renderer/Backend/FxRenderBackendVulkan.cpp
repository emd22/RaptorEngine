#include "FxRenderBackendVulkan.hpp"

#include <Core/Defines.hpp>
#include <Core/Types.hpp>

#include <Core/Log.hpp>

#include "Renderer/Backend/Vulkan/Fence.hpp"
#include "Renderer/Backend/Vulkan/Pipeline.hpp"
#include "Renderer/Backend/Vulkan/Semaphore.hpp"
#include "Vulkan/FxCommandPool.hpp"
#include "Vulkan/FxCommandBuffer.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include <vulkan/vulkan.h>

#include "Vulkan/Util.hpp"
#include <Core/FxPanic.hpp>
#include "vulkan/vulkan_core.h"

#include <ThirdParty/vk_mem_alloc.h>

#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL.h>

#define FX_VULKAN_DEBUG 1

namespace vulkan {

using ExtensionNames = FxRenderBackendVulkan::ExtensionNames;
using ExtensionList = FxRenderBackendVulkan::ExtensionList;

FX_SET_MODULE_NAME("Vulkan")

ExtensionNames FxRenderBackendVulkan::CheckExtensionsAvailable(ExtensionNames &requested_extensions)
{
    if (mAvailableExtensions.IsEmpty()) {
        QueryInstanceExtensions();
    }

    std::vector<const char *> missing_extensions;

    for (const char *requested_name : requested_extensions) {
        bool found_extension = false;
        for (const auto &extension : mAvailableExtensions) {
            if (!strncmp(extension.extensionName, requested_name, 256)) {
                found_extension = true;
                break;
            }
        }

        if (!found_extension) {
            missing_extensions.push_back(requested_name);
        }
    }

    return missing_extensions;
}

StaticArray<VkLayerProperties> FxRenderBackendVulkan::GetAvailableValidationLayers()
{
    uint32 layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    StaticArray<VkLayerProperties> validation_layers;
    validation_layers.InitSize(layer_count);

    vkEnumerateInstanceLayerProperties(&layer_count, validation_layers.Data);

    return validation_layers;
}

VkDebugUtilsMessengerEXT CreateDebugMessenger(VkInstance instance);

void FxRenderBackendVulkan::Init(Vec2i window_size)
{
    InitVulkan();
    CreateSurfaceFromWindow();

    mDevice.Create(mInstance, mWindowSurface);

    InitGPUAllocator();
    Swapchain.Init(window_size, mWindowSurface, &mDevice);

    InitFrames();
    InitUploadContext();

    Initialized = true;
}

void FxRenderBackendVulkan::InitUploadContext()
{
    UploadContext.CommandPool.Create(GetDevice(), GetDevice()->mQueueFamilies.GetTransferFamily());
    UploadContext.CommandBuffer.Create(&UploadContext.CommandPool);

    UploadContext.UploadFence.Create(GetDevice());
    UploadContext.UploadFence.Reset();
}

void FxRenderBackendVulkan::DestroyUploadContext()
{
    UploadContext.UploadFence.Destroy();
    UploadContext.CommandBuffer.Destroy();
    UploadContext.CommandPool.Destroy();
}

void FxRenderBackendVulkan::InitFrames()
{
    Frames.InitSize(FramesInFlight);

    const uint32 graphics_family = GetDevice()->mQueueFamilies.GetGraphicsFamily();

    GPUDevice *device = GetDevice();

    for (int i = 0; i < Frames.Size; i++) {
        Frames.Data[i].CommandPool.Create(device, graphics_family);
        Frames.Data[i].CommandBuffer.Create(&Frames.Data[i].CommandPool);

        Frames.Data[i].Create(device);
    }
}

void FxRenderBackendVulkan::DestroyFrames()
{
    vkQueueWaitIdle(GetDevice()->GraphicsQueue);

    for (auto &frame : Frames) {
        frame.CommandBuffer.Destroy();
        frame.CommandPool.Destroy();

        frame.Destroy();
    }

    Frames.Free();
}

void FxRenderBackendVulkan::InitVulkan()
{
    const char* app_name = "Rocket";
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = app_name;
    app_info.pEngineName = app_name;
    app_info.apiVersion = VK_MAKE_VERSION(1, 3, 261);

    ExtensionNames requested_extensions = {
        VK_EXT_LAYER_SETTINGS_EXTENSION_NAME,
    };

    ExtensionNames all_extensions = MakeInstanceExtensionList(requested_extensions);

#ifdef FX_VULKAN_DEBUG
    all_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    all_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

    std::cout << "Requested to load " << all_extensions.size() << " extensions...\n";

    for (const auto& extension : all_extensions) {
        Log::Debug("Ext: %s", extension);
    }

    ExtensionNames missing_extensions = CheckExtensionsAvailable(all_extensions);

    if (!missing_extensions.empty()) {
        std::cerr << "MISSING: ";
        for (size_t i = 0; i < missing_extensions.size(); ++i) {
            std::cerr << missing_extensions[i];
            if (i < missing_extensions.size() - 1) {
                std::cerr << ", ";
            }
        }

        FxPanic("Missing required instance extensions", 0);
    }

    // auto validation_layers = GetAvailableValidationLayers();
    // for (auto &layer : validation_layers) {
    //     Log::Debug("Layer: %s", layer.layerName);
    // }

    std::vector<const char*> requested_validation_layers = {
        "VK_LAYER_KHRONOS_validation",
        // "VK_LAYER_KHRONOS_shader_object",
    };

    VkInstanceCreateInfo instance_info = {};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.ppEnabledExtensionNames = all_extensions.data();
    instance_info.enabledExtensionCount = static_cast<uint32_t>(all_extensions.size());
    instance_info.ppEnabledLayerNames = requested_validation_layers.data();
    instance_info.enabledLayerCount = static_cast<uint32_t>(requested_validation_layers.size());
    instance_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    VkResult result = vkCreateInstance(&instance_info, nullptr, &mInstance);

    if (result != VK_SUCCESS) {
        FxPanic("Could not create vulkan instance!", result);
    }

#ifdef FX_VULKAN_DEBUG
    mDebugMessenger = CreateDebugMessenger(mInstance);
    if (!mDebugMessenger) {
        FxPanic("Could not create debug messenger", 0);
    }
#endif

    Initialized = true;
}

uint32 DebugMessageCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, uint32 type,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
    void *user_data
) {
    const char * message = callback_data->pMessage;
    const char *fmt = "VkValidator: %s";

    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) {
        Log::Error(fmt, message);
    }
    else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) {
        Log::Warning(fmt, message);
    }
    else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)) {
        // Log::Info(fmt, message);
    }
    else {
        Log::Debug(fmt, message);
    }

    return 0;
}

template<typename FuncProt>
FuncProt GetExtensionFuncVk(VkInstance instance, const char *name)
{
    PFN_vkVoidFunction raw_ptr = vkGetInstanceProcAddr(instance, name);

    if (raw_ptr != nullptr) {
        return (FuncProt)raw_ptr;
    }

    Log::Error("Could not load extension function '%s'", name);
    return nullptr;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger)
{
    using FuncSig = VkResult (*)(VkInstance, const VkDebugUtilsMessengerCreateInfoEXT *, const VkAllocationCallbacks *, VkDebugUtilsMessengerEXT *);

    const auto returned_function = GetExtensionFuncVk<FuncSig>(instance, "vkCreateDebugUtilsMessengerEXT");
    return returned_function(instance, pCreateInfo, pAllocator, pDebugMessenger);
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks *pAllocator) {
    using FuncSig = void (*)(VkInstance, VkDebugUtilsMessengerEXT, const VkAllocationCallbacks *);

    const auto returned_function = GetExtensionFuncVk<FuncSig>(instance, "vkDestroyDebugUtilsMessengerEXT");
    return returned_function(instance, messenger, pAllocator);
}


VkDebugUtilsMessengerEXT CreateDebugMessenger(VkInstance instance)
{
    const auto severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;

    const auto message_type = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;


    VkDebugUtilsMessengerCreateInfoEXT create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = severity,
        .messageType = message_type,
        .pfnUserCallback = DebugMessageCallback
    };

    VkDebugUtilsMessengerEXT messenger;

    const auto status = CreateDebugUtilsMessengerEXT(instance, &create_info, nullptr, &messenger);
    if (status != VK_SUCCESS) {
        Log::Error("Could not create debug messenger! (err: %s)", VulkanUtil::ResultToStr(status));
        return nullptr;
    }

    return messenger;
}

void DestroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
    if (messenger == nullptr) {
        return;
    }

    DestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
}

void FxRenderBackendVulkan::InitGPUAllocator()
{
    const GPUDevice *device = GetDevice();

    const VmaAllocatorCreateInfo create_info = {
        .physicalDevice = device->Physical,
        .device = device->Device,
        .instance = mInstance
    };

    const VkResult status = vmaCreateAllocator(&create_info, &GPUAllocator);
    if (status != VK_SUCCESS) {
        FxPanic("Could not create VMA allocator!", status);
    }
}

void FxRenderBackendVulkan::DestroyGPUAllocator()
{
    vmaDestroyAllocator(GPUAllocator);
}

ExtensionNames FxRenderBackendVulkan::MakeInstanceExtensionList(ExtensionNames &user_requested_extensions)
{
    uint32 required_extension_count = 0;
    const char * const * required_extensions = SDL_Vulkan_GetInstanceExtensions(&required_extension_count);

    QueryInstanceExtensions();

    const uint32 total_extensions_size = user_requested_extensions.size() + required_extension_count;
    ExtensionNames total_extensions;
    total_extensions.reserve(total_extensions_size);

    // append the user requested extensions
    total_extensions.insert(total_extensions.begin(), user_requested_extensions.begin(), user_requested_extensions.end());

    for (int32 i = 0; i < required_extension_count; i++) {
        total_extensions.push_back(required_extensions[i]);
    }

    return total_extensions;
}

ExtensionList &FxRenderBackendVulkan::QueryInstanceExtensions(bool invalidate_previous)
{
    Log::Debug("Query Extensions", 0);


    if (mAvailableExtensions.IsNotEmpty()) {
        if (invalidate_previous) {
            mAvailableExtensions.Free();
        }
        else {
            return mAvailableExtensions;
        }
    }

    // Get the count of the current extensions
    uint32_t extension_count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not query instance extensions!");
    }

    std::cout << "Ext count: " << extension_count << "\n";

    // resize to create the items in the vector
    mAvailableExtensions.InitSize(extension_count);

    // Get the available instance extensions
    result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, mAvailableExtensions.Data);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not query instance extensions!");
    }

    // std::cout << "=== Available Instance Extensions (" << extension_count << ") ===\n";

    // for (uint32_t i = 0; i < extension_count; ++i) {
    //     const VkExtensionProperties& extension = mAvailableExtensions[i];
    //     std::cout << extension.extensionName << " : " << extension.specVersion << "\n";
    // }

    return mAvailableExtensions;
}

void FxRenderBackendVulkan::SubmitUploadCmd(FxRenderBackendVulkan::UploadFunc upload_func)
{
    FxCommandBuffer &cmd = UploadContext.CommandBuffer;

    cmd.Record(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    upload_func(cmd);

    cmd.End();

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

        .commandBufferCount = 1,
        .pCommandBuffers = &cmd.CommandBuffer,
    };

    Log::Debug(
        "cmd thread: %lu",
        std::this_thread::get_id()
    );

    VkTry(
        vkQueueSubmit(GetDevice()->TransferQueue, 1, &submit_info, UploadContext.UploadFence.Fence),
        "Error submitting upload buffer"
    );

    UploadContext.UploadFence.WaitFor();
    UploadContext.UploadFence.Reset();

    UploadContext.CommandPool.Reset();
}

FrameResult FxRenderBackendVulkan::BeginFrame(GraphicsPipeline &pipeline, Mat4f &MVPMatrix)
{
    FrameData *frame = GetFrame();

    frame->InFlight.WaitFor();

    FrameResult result = GetNextSwapchainImage(frame);
    if (result != FrameResult::Success) {
        return result;
    }

    frame->InFlight.Reset();

    frame->CommandBuffer.Reset();
    frame->CommandBuffer.Record();

    pipeline.RenderPass.Begin();
    pipeline.Bind(frame->CommandBuffer);

    const int32 width = Swapchain.Extent.Width();
    const int32 height = Swapchain.Extent.Height();

    const VkViewport viewport = {
        .x = 0, .y = 0,
        .width = (float32)width,
        .height = (float32)height,
        .minDepth = 0.0,
        .maxDepth = 1.0,
    };

    vkCmdSetViewport(frame->CommandBuffer.CommandBuffer, 0, 1, &viewport);

    const VkRect2D scissor = {
        .offset = { .x = 0, .y = 0 },
        .extent = { .width = (uint32)width, .height = (uint32)height }
    };

    vkCmdSetScissor(frame->CommandBuffer.CommandBuffer, 0, 1, &scissor);


    DrawPushConstants push_constants{};

    memcpy(push_constants.MVPMatrix, MVPMatrix.RawData, sizeof(float32) * 16);

    vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, pipeline.Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);

    return FrameResult::Success;
}

void FxRenderBackendVulkan::SubmitFrame()
{
    FrameData *frame = GetFrame();

    const VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->ImageAvailable.Semaphore,
        .pWaitDstStageMask = wait_stages,
        // command buffers
        .commandBufferCount = 1,
        .pCommandBuffers = &frame->CommandBuffer.CommandBuffer,
        // signal semaphores
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &frame->RenderFinished.Semaphore
    };

    VkTry(
        vkQueueSubmit(GetDevice()->GraphicsQueue, 1, &submit_info, frame->InFlight.Fence),
        "Error submitting draw buffer"
    );
}

void FxRenderBackendVulkan::PresentFrame()
{
    FrameData *frame = GetFrame();

    if (Swapchain.Initialized != true) {
        FxPanic("Swapchain not initialized!", 0);
    }

    const VkSwapchainKHR swapchains[] = {
        Swapchain.GetSwapchain(),
    };

    const VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->RenderFinished.Semaphore,

        .swapchainCount = 1,
        .pSwapchains = swapchains,

        .pImageIndices = &mImageIndex,

        .pResults = nullptr,
    };

    const VkResult status = vkQueuePresentKHR(GetDevice()->PresentQueue, &present_info);

    if (status == VK_SUCCESS) { }
    else if (status == VK_ERROR_OUT_OF_DATE_KHR || status == VK_SUBOPTIMAL_KHR) {
        // Swapchain.Rebuild()..
    }
    else {
        Log::Error("Error submitting present queue", status);
    }
}

void FxRenderBackendVulkan::FinishFrame(GraphicsPipeline &pipeline)
{
    pipeline.RenderPass.End();

    GetFrame()->CommandBuffer.End();

    SubmitFrame();
    PresentFrame();

    ProcessDeletionQueue();

    mInternalFrameCounter++;

    // better to do one division per frame as opposed to one every GetFrameNumber()
    mFrameNumber = (mInternalFrameCounter) % FramesInFlight;
}

FrameResult FxRenderBackendVulkan::GetNextSwapchainImage(FrameData *frame)
{
    const uint64 timeout = UINT64_MAX; // TODO: change this value and handle AcquireNextImage errors correctly

    const VkResult result = vkAcquireNextImageKHR(
        GetDevice()->Device,
        Swapchain.GetSwapchain(),
        timeout,
        frame->ImageAvailable.Semaphore,
        nullptr,
        &mImageIndex
    );

    if (result == VK_SUCCESS) {
        return FrameResult::Success;
    }
    else if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Swapchain.Rebuild()..
        return FrameResult::GraphicsOutOfDate;
    }
    else {
        Log::Error("Error getting next swapchain image!", result);
    }

    return FrameResult::RenderError;
}

void FxRenderBackendVulkan::CreateSurfaceFromWindow()
{
    if (mWindow == nullptr) {
        FxPanic("No window attached! use FxRenderBackendVulkan::SelectWindow()", 0);
    }

    bool success = SDL_Vulkan_CreateSurface(mWindow->GetWindow(), mInstance, nullptr, &mWindowSurface);

    if (!success) {
        FxPanic("Could not attach Vulkan instance to window! (SDL err: %s)", SDL_GetError());
    }
}

void FxRenderBackendVulkan::Destroy()
{
    GetDevice()->WaitForIdle();

    DestroyUploadContext();
    DestroyFrames();

    mInDeletionQueue.store(true);
    while (!mDeletionQueue.empty()) {
        ProcessDeletionQueue(true);
        // insert a small delay to avoid the processor spinning out while
        // waiting for an object. this allows handing the core off to other threads.
        std::this_thread::sleep_for(std::chrono::nanoseconds(100));
    }
    mInDeletionQueue.store(false);

    GetDevice()->WaitForIdle();

    Swapchain.Destroy();
    DestroyGPUAllocator();

    if (mWindowSurface) {
        vkDestroySurfaceKHR(mInstance, mWindowSurface, nullptr);
    }

    GetDevice()->Destroy();
    DestroyDebugMessenger(mInstance, mDebugMessenger);
    vkDestroyInstance(mInstance, nullptr);

    Initialized = false;
}

FrameData *FxRenderBackendVulkan::GetFrame()
{
    return &Frames[GetFrameNumber()];
}

}; // namespace vulkan
