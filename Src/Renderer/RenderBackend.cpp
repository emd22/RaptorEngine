#include "RenderBackend.hpp"

#include "Backend/Commands.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Synchro.hpp"
#include "Backend/Util.hpp"
#include "Constants.hpp"
#include "DeferredRenderer.hpp"
#include "Engine.hpp"
#include "ObjectManager.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <Asset/Animation.hpp>
#include <Core/Defines.hpp>
#include <Core/Panic.hpp>
#include <Core/RefUtil.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/ExtensionHandles.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/State.hpp>
#include <thread>
#include <vector>

#define FX_VULKAN_DEBUG 1

namespace fx::renderer {

using ExtensionNames = RenderBackend::ExtensionNames;
using ExtensionList = RenderBackend::ExtensionList;

FX_SET_MODULE_NAME("RenderBackend")

ExtensionNames RenderBackend::CheckExtensionsAvailable(ExtensionNames& requested_extensions)
{
    if (mAvailableExtensions.IsEmpty()) {
        QueryInstanceExtensions();
    }

    std::vector<const char*> missing_extensions;

    for (const char* requested_name : requested_extensions) {
        bool found_extension = false;
        for (const auto& extension : mAvailableExtensions) {
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

SizedArray<VkLayerProperties> RenderBackend::GetAvailableValidationLayers()
{
    uint32 layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    SizedArray<VkLayerProperties> validation_layers;
    validation_layers.InitSize(layer_count);

    vkEnumerateInstanceLayerProperties(&layer_count, validation_layers.pData);

    return validation_layers;
}

VkDebugUtilsMessengerEXT CreateDebugMessenger(VkInstance instance);

void RenderBackend::Init(Vec2u window_size)
{
    InitVulkan();
    CreateSurfaceFromWindow();

    mDevice.Create(mInstance, mWindowSurface);

    InitGPUAllocator();
    Swapchain.Init(window_size, mWindowSurface, &mDevice);

    InitFrames();
    InitUploadContext();

    // Create final submission semaphores. Note that there is one submission semaphore
    // per Swapchain image, not frame in flight.
    mSubmitSemaphores.InitSize(Swapchain.OutputImages.Size);
    for (Semaphore& sem : mSubmitSemaphores) {
        sem.Create();
    }

    gObjectManager->Create();

    LightBuffer.Create(scLightUniformSize, Limits::MaxActiveLights);
    BoneBuffer.Create(Limits::MaxBones * sizeof(Mat4f), 1);

    Mat4f initial_matrix = Mat4f::sIdentity;
    BoneBuffer.SetAllValues(initial_matrix.RawData, true);

    pDeferredRenderer = MakeRef<DeferredRenderer>();
    pDeferredRenderer->Create(Swapchain.Extent);

    bInitialized = true;
}

void RenderBackend::InitUploadContext()
{
    UploadContext.CommandPool.Create(GetDevice(), GetDevice()->mQueueFamilies.GetTransferFamily());
    UploadContext.CommandBuffer.Create(&UploadContext.CommandPool);

    UploadContext.UploadFence.Create();
    UploadContext.UploadFence.Reset();
}

void RenderBackend::DestroyUploadContext()
{
    UploadContext.UploadFence.Destroy();
    UploadContext.CommandBuffer.Destroy();
    UploadContext.CommandPool.Destroy();
}

void RenderBackend::InitFrames()
{
    Frames.InitSize(FramesInFlight);

    const uint32 graphics_family = GetDevice()->mQueueFamilies.GetGraphicsFamily();

    GpuDevice* device = GetDevice();

    for (int i = 0; i < Frames.Size; i++) {
        FrameData& frame = Frames.pData[i];
        frame.CommandPool.Create(device, graphics_family);
        frame.CommandBuffer.Create(&frame.CommandPool);
        /*frame.ShadowCommandBuffer.Create(&frame.CommandPool);
        frame.CompCommandBuffer.Create(&frame.CommandPool);
        frame.LightCommandBuffer.Create(&frame.CommandPool);*/

        frame.Create(device);

        auto synchro_label = std::format("Frame {} I.A.", i);
        Util::SetDebugLabel(synchro_label.c_str(), VK_OBJECT_TYPE_SEMAPHORE, frame.ImageAvailable.Get());

        synchro_label = std::format("Frame {} G.P", i);
        Util::SetDebugLabel(synchro_label.c_str(), VK_OBJECT_TYPE_SEMAPHORE, frame.OffscreenSem.Get());

        synchro_label = std::format("Frame {} R.F", i);
        Util::SetDebugLabel(synchro_label.c_str(), VK_OBJECT_TYPE_SEMAPHORE, frame.RenderFinished.Get());
    }
}

void RenderBackend::DestroyFrames()
{
    {
        SpinLockContext<VkQueue> graphics_queue = GetDevice()->GetGraphicsQueue();

        vkQueueWaitIdle(graphics_queue.Get());
    }

    for (auto& frame : Frames) {
        // frame.DescriptorSet.Destroy();
        frame.CompDescriptorSet.Destroy();

        frame.CommandBuffer.Destroy();

        frame.Destroy();
    }

    Frames.Free();
}

void RenderBackend::InitVulkan()
{
    const char* app_name = "Foxtrot";
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = app_name;
    app_info.pEngineName = app_name;
    app_info.apiVersion = VK_MAKE_VERSION(1, 3, 261);

    ExtensionNames requested_extensions = {
        // VK_EXT_LAYER_SETTINGS_EXTENSION_NAME,
    };

    ExtensionNames all_extensions = MakeInstanceExtensionList(requested_extensions);

#ifdef FX_VULKAN_DEBUG
    all_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    all_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

    std::cout << "Requested to load " << all_extensions.size() << " extensions...\n";

    for (const auto& extension : all_extensions) {
        LogDebug("Ext: {:s}", extension);
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

        ModulePanic("Missing required instance extensions");
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
#ifdef FX_USE_PORTABILITY_EXTENSION
    instance_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    VkResult result = vkCreateInstance(&instance_info, nullptr, &mInstance);

    if (result != VK_SUCCESS) {
        ModulePanicVulkan("Could not create vulkan instance!", result);
    }

#ifdef FX_VULKAN_DEBUG
    mDebugMessenger = CreateDebugMessenger(mInstance);
    if (!mDebugMessenger) {
        ModulePanic("Could not create debug messenger");
    }
#endif

    bInitialized = true;
}

uint32 DebugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, uint32 type,
                            const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
{
    const char* message = callback_data->pMessage;
    const char* fmt = "VkValidator: {:s}";


    if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) {
        LogError(fmt, message);
    }
    else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) {
        LogWarning(fmt, message);
    }
    else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)) {
        // Log::Info(fmt, message);
    }
    else {
        LogDebug(fmt, message);
    }

    return 0;
}

// template<typename FuncProt>
// FuncProt GetExtensionFuncVk(VkInstance instance, const char *name)
// {
//     PFN_vkVoidFunction raw_ptr = vkGetInstanceProcAddr(instance, name);

//     if (raw_ptr != nullptr) {
//         return (FuncProt)raw_ptr;
//     }

//     Log::Error("Could not load extension function '%s'", name);
//     return nullptr;
// }


VkDebugUtilsMessengerEXT CreateDebugMessenger(VkInstance instance)
{
    const auto severity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;

    const auto message_type = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;


    VkDebugUtilsMessengerCreateInfoEXT create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = severity,
        .messageType = message_type,
        .pfnUserCallback = DebugMessageCallback,
    };

    VkDebugUtilsMessengerEXT messenger;

    const auto status = Rx_EXT_CreateDebugUtilsMessenger(instance, &create_info, nullptr, &messenger);
    if (status != VK_SUCCESS) {
        LogError("Could not create debug messenger! (err: {:s})", Util::ResultToStr(status));
        return nullptr;
    }

    return messenger;
}

void DestroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    if (messenger == nullptr) {
        return;
    }

    Rx_EXT_DestroyDebugUtilsMessenger(instance, messenger, nullptr);
}

void RenderBackend::InitGPUAllocator()
{
    const GpuDevice* device = GetDevice();

    const VmaAllocatorCreateInfo create_info = { .physicalDevice = device->Physical,
                                                 .device = device->Device,
                                                 .instance = mInstance };

    const VkResult status = vmaCreateAllocator(&create_info, &GpuAllocator);
    if (status != VK_SUCCESS) {
        ModulePanicVulkan("Could not create VMA allocator!", status);
    }
}

void RenderBackend::DestroyGPUAllocator() { vmaDestroyAllocator(GpuAllocator); }

ExtensionNames RenderBackend::MakeInstanceExtensionList(ExtensionNames& user_requested_extensions)
{
    uint32 required_extension_count = 0;
    const char* const* required_extensions = SDL_Vulkan_GetInstanceExtensions(&required_extension_count);

    QueryInstanceExtensions();

    const uint32 total_extensions_size = user_requested_extensions.size() + required_extension_count;
    ExtensionNames total_extensions;
    total_extensions.reserve(total_extensions_size);

    // append the user requested extensions
    total_extensions.insert(total_extensions.begin(), user_requested_extensions.begin(),
                            user_requested_extensions.end());

    for (int32 i = 0; i < required_extension_count; i++) {
        total_extensions.push_back(required_extensions[i]);
    }

    return total_extensions;
}

ExtensionList& RenderBackend::QueryInstanceExtensions(bool invalidate_previous)
{
    LogDebug("Querying for instance extensions...");


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
    result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, mAvailableExtensions.pData);
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

void RenderBackend::SubmitPushConstantsRaw(const CommandBuffer& cmd, const Pipeline& pipeline, eShaderType shader_types,
                                           const void* data, uint32 data_size) const
{
    DebugAssert(pipeline.Layout2.IsValid());

    // Currently, there is nowhere in the engine that requires two separate PC buffers and therefore requires an offset.
    // As well, the small required size of a PC kind of makes this useless. For now, we will ignore this and if needed
    // there will be an updated version of this function.
    // I'm pretty sure when I was using Slang I had one shader that required this, but thats since been cacked..
    static constexpr uint32 scOffset = 0;
    vkCmdPushConstants(cmd.Get(), pipeline.Layout2.Get(), ShaderUtil::ToUnderlyingType(shader_types), scOffset,
                       data_size, data);
}


void RenderBackend::SubmitUploadCmd(RenderBackend::SubmitFunc upload_func)
{
    CommandBuffer& cmd = UploadContext.CommandBuffer;

    cmd.Record(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    upload_func(cmd);
    cmd.End();

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

        .commandBufferCount = 1,
        .pCommandBuffers = &cmd.CommandBuffer,
    };

    // Log::Debug(
    //     "cmd thread: %lu",
    //     std::this_thread::get_id()
    // );

    SpinLockContext<VkQueue> transfer_queue = GetDevice()->GetTransferQueue();

    VkTry(vkQueueSubmit(transfer_queue.Get(), 1, &submit_info, UploadContext.UploadFence.Fence),
          "Error submitting upload buffer");

    UploadContext.UploadFence.WaitFor();
    UploadContext.UploadFence.Reset();

    UploadContext.CommandPool.Reset();
}


void RenderBackend::SubmitOneTimeCmd(RenderBackend::SubmitFunc submit_func)
{
    CommandBuffer cmd;
    cmd.Create(&GetFrame()->CommandPool);

    cmd.Record(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    submit_func(cmd);
    cmd.End();

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

        .commandBufferCount = 1,
        .pCommandBuffers = &cmd.CommandBuffer,
    };

    SpinLockContext<VkQueue> graphics_queue = GetDevice()->GetGraphicsQueue();

    VkTry(vkQueueSubmit(graphics_queue.Get(), 1, &submit_info, nullptr), "Error submitting upload buffer");
    vkQueueWaitIdle(graphics_queue.Get());

    graphics_queue.Unlock();

    cmd.Reset();
    cmd.Destroy();
}


eFrameResult RenderBackend::BeginFrame()
{
    FrameData* frame = GetFrame();

    LightBuffer.Rewind();

    // memcpy(GetUbo().MvpMatrix.RawData, MVPMatrix.RawData, sizeof(Mat4f));

    frame->InFlight.WaitFor();
    frame->InFlight.Reset();


    eFrameResult result = GetNextSwapchainImage(frame);
    if (result != eFrameResult::Success) {
        return result;
    }

    return eFrameResult::Success;
}

void RenderBackend::BeginGeometry()
{
    FrameData* frame = GetFrame();

    pDeferredRenderer->GPass.Begin(frame->CommandBuffer, *pDeferredRenderer->pGeometryPipeline);
}

void RenderBackend::PresentFrame()
{
    FrameData* frame = GetFrame();

    const VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    };

    VkSemaphore submit_semaphore = mSubmitSemaphores[mImageIndex].Get();

    VkSemaphore wait_semaphore = frame->ImageAvailable.Get();

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &wait_semaphore,
        .pWaitDstStageMask = wait_stages,

        .commandBufferCount = 1,
        .pCommandBuffers = &frame->CommandBuffer.CommandBuffer,

        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &submit_semaphore,
    };

    {
        SpinLockContext<VkQueue> graphics_queue = GetDevice()->GetGraphicsQueue();

        VkTry(vkQueueSubmit(graphics_queue.Get(), 1, &submit_info, frame->InFlight.Fence),
              "Error submitting draw buffer");
    }


    SpinLockContext<VkQueue> present_queue = GetDevice()->GetPresentQueue();

    if (Swapchain.bInitialized != true) {
        ModulePanic("Swapchain not initialized!");
    }

    const VkSwapchainKHR swapchains[] = {
        Swapchain.GetSwapchain(),
    };

    const VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &submit_semaphore,

        .swapchainCount = 1,
        .pSwapchains = swapchains,

        .pImageIndices = &mImageIndex,

        .pResults = nullptr,
    };

    const VkResult status = vkQueuePresentKHR(present_queue.Get(), &present_info);

    if (status == VK_SUCCESS) {
    }
    else if (status == VK_ERROR_OUT_OF_DATE_KHR || status == VK_SUBOPTIMAL_KHR) {
        // Swapchain.Rebuild()..
    }
    else {
        LogError("Error submitting present queue. Status: {:x}", static_cast<int32>(status));
    }
}

void RenderBackend::BeginLighting()
{
    FrameData* frame = GetFrame();

    pDeferredRenderer->GPass.End();

    Target* depth_target = pDeferredRenderer->GPass.GetTarget(eImageFormat::eD32_Float, 0);
    Assert(depth_target != nullptr);
    depth_target->Image.TransitionDepthToShaderRO(frame->CommandBuffer);


    pDeferredRenderer->LightPass.Begin(frame->CommandBuffer,
                                       gPipelineCache->Request(ePipelineName::LightingDirectional));

    // gState->BufferOffset(ShaderType::Vertex, gRenderer->Uniforms.GetBaseOffset());
    // gState->Pipeline(&pDeferredRenderer->PlLightingDirectional);
    // gState->Apply(frame->CommandBuffer);
    // gState->Reset();


    pDeferredRenderer->DsLighting.BindWithOffset(0, frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 gPipelineCache->Request(ePipelineName::LightingDirectional),
                                                 gRenderer->LightBuffer.GetBaseOffset());
}


void RenderBackend::BeginUnlit()
{
    FrameData* frame = GetFrame();

    pDeferredRenderer->LightPass.End();


    // Target* depth_target = gRenderer->pDeferredRenderer->GPass.GetTarget(ImageFormat::eD32_Float, 0);
    // Assert(depth_target != nullptr);
    // depth_target->Image.TransitionDepthToAttachment(gRenderer->GetFrame()->CommandBuffer);

    pDeferredRenderer->RpForward.Begin(&frame->CommandBuffer, pDeferredRenderer->FbForward.Get(),
                                       Slice<VkClearValue>({}, 0));

    // pDeferredRenderer->PlUnlit.Bind(frame->CommandBuffer);
    gPipelineCache->Bind(ePipelineName::Unlit, frame->CommandBuffer);
}

void RenderBackend::DoComposition(Camera& render_cam)
{
    FrameData* frame = GetFrame();

    pDeferredRenderer->RpForward.End();

    pDeferredRenderer->CompPass.Begin(frame->CommandBuffer, gPipelineCache->Request(ePipelineName::Composition));
    pDeferredRenderer->DoCompPass(render_cam);

    pDeferredRenderer->CompPass.End();
    frame->CommandBuffer.End();

    PresentFrame();
    ProcessDeletionQueue();

    mInternalFrameCounter++;

    mFrameNumber = (mInternalFrameCounter % FramesInFlight);
}

eFrameResult RenderBackend::GetNextSwapchainImage(FrameData* frame)
{
    const uint64 timeout = UINT64_MAX; // TODO: change this value and handle AcquireNextImage errors correctly

    const VkResult result = vkAcquireNextImageKHR(GetDevice()->Device, Swapchain.GetSwapchain(), timeout,
                                                  frame->ImageAvailable.Get(), nullptr, &mImageIndex);

    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        return eFrameResult::Success;
    }
    else if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain.Rebuild()..
        return eFrameResult::GraphicsOutOfDate;
    }
    else {
        LogError("Error getting next swapchain image! Status: {:x}", static_cast<int>(result));
    }

    return eFrameResult::RenderError;
}

void RenderBackend::CreateSurfaceFromWindow()
{
    if (mpWindow == nullptr) {
        ModulePanic("No window attached! use RenderBackend::SelectWindow()");
    }

    bool success = SDL_Vulkan_CreateSurface(mpWindow->GetWindow(), mInstance, nullptr, &mWindowSurface);

    if (!success) {
        ModulePanic("Could not attach Vulkan instance to window! (SDL err: {})", SDL_GetError());
    }
}


void RenderBackend::Destroy()
{
    GetDevice()->WaitForIdle();

    DestroyUploadContext();
    DestroyFrames();

    for (Semaphore& sem : mSubmitSemaphores) {
        sem.Destroy();
    }

    LightBuffer.Destroy();
    BoneBuffer.Destroy();


    while (!mDeletionQueue.empty()) {
        ProcessDeletionQueue(true);

        // insert a small delay to avoid the processor spinning out while
        // waiting for an object. this allows handing the core off to other threads.
        std::this_thread::sleep_for(std::chrono::nanoseconds(100));
    }


    CompDescriptorPool.Destroy();

    GetDevice()->WaitForIdle();

    Swapchain.Destroy();

    DestroyGPUAllocator();

    if (mWindowSurface) {
        vkDestroySurfaceKHR(mInstance, mWindowSurface, nullptr);
    }

    GetDevice()->Destroy();

    DestroyDebugMessenger(mInstance, mDebugMessenger);
    vkDestroyInstance(mInstance, nullptr);

    bInitialized = false;
}

FrameData* RenderBackend::GetFrame() { return &Frames[GetFrameNumber()]; }

} // namespace fx::renderer
