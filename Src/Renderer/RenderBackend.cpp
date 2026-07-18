#include "RenderBackend.hpp"

#include "Backend/Commands.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Synchro.hpp"
#include "Backend/Util.hpp"
#include "Constants.hpp"
#include "DeferredRenderer.hpp"
#include "Engine.hpp"
#include "Object/ObjectManager.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <Asset/Animation.hpp>
#include <Asset/AssetManager.hpp>
#include <Core/Assert.hpp>
#include <Core/Defines.hpp>
#include <Core/RefUtil.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/ExtensionHandles.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/PSOBuild.hpp>
#include <Renderer/PipelineCache.hpp>
#include <thread>
#include <vector>


#define FX_VULKAN_DEBUG 1

// This isn't defined on some platforms/drivers.
#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif

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

bool RenderBackend::RequiresVulkanPortability()
{
	if (mAvailableExtensions.IsEmpty()) {
		QueryInstanceExtensions();
	}

	for (const VkExtensionProperties& extension : mAvailableExtensions) {
		if (!strncmp(extension.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, 256)) {
			return true;
		}
	}

	return false;
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

	SpinLockContext<Queue<DeletionObject>> deletion_queue = mDeletionQueue.GetQueue();
	deletion_queue->InitCapacity(Limits::MaxDeletionQueueItems);

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

	pDeferredRenderer = new DeferredRenderer;
	pDeferredRenderer->Create(Swapchain.Extent);

	bInitialized = true;
}

void RenderBackend::InitUploadContext()
{
	UploadContext.CmdPool.Create(GetDevice(), GetDevice()->mQueueFamilies.GetTransferFamily());
	UploadContext.CmdBuffer.Create(&UploadContext.CmdPool);
	UploadContext.ImmediateCmdBuffer.Create(&UploadContext.CmdPool);

	Util::SetDebugLabel("UploadImmediate", VK_OBJECT_TYPE_COMMAND_BUFFER, UploadContext.ImmediateCmdBuffer.Cmd);
	Util::SetDebugLabel("Upload", VK_OBJECT_TYPE_COMMAND_BUFFER, UploadContext.CmdBuffer.Cmd);

	UploadContext.UploadFence.Create();
	UploadContext.ImmediateUploadFence.Create();
}

void RenderBackend::DestroyUploadContext()
{
	UploadContext.CmdBuffer.Destroy();
	UploadContext.ImmediateCmdBuffer.Destroy();
	UploadContext.CmdPool.Destroy();

	UploadContext.UploadFence.Destroy();
	UploadContext.ImmediateUploadFence.Destroy();
}

void RenderBackend::InitFrames()
{
	Frames.InitSize(FramesInFlight);

	const uint32 graphics_family = GetDevice()->mQueueFamilies.GetGraphicsFamily();

	GpuDevice* device = GetDevice();

	for (int i = 0; i < Frames.Size; i++) {
		FrameData& frame = Frames.pData[i];
		frame.CmdPool.Create(device, graphics_family);
		frame.CmdBuffer.Create(&frame.CmdPool);

		Util::SetDebugLabel("RenderCmd", VK_OBJECT_TYPE_COMMAND_BUFFER, frame.CmdBuffer.Cmd);

		frame.InFlight.Create();
		// frame.InFlight.Reset();

		frame.Create(device);

		auto synchro_label = std::format("Frame {} I.A.", i);
		Util::SetDebugLabel(synchro_label.c_str(), VK_OBJECT_TYPE_SEMAPHORE, frame.ImageAvailable.Get());

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

		frame.CmdBuffer.Destroy();
		frame.CmdPool.Destroy();

		frame.Destroy();
	}

	Frames.Free();
}

void RenderBackend::RebuildRenderStages()
{
	DeferredRenderer* rd = pDeferredRenderer;

	Vec2u size = GetWindow()->GetSize();

	rd->GPass.Rebuild(size);
	rd->CompPass.Rebuild(size);
	rd->LightPass.Rebuild(size);
	rd->ForwardPass.Rebuild(size);

	rd->DescriptorPool.Recreate();
	rd->CreateDescriptorSets();

	/*for (FrameData& frame : Frames) {
		frame.InFlight.Reset();
	}*/
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

	LogDebug(LC_RENDER, "== Supported Extensions ==");
	for (const VkExtensionProperties& extension : mAvailableExtensions) {
		LogDebug(LC_RENDER, "{}", extension.extensionName);
	}

	ExtensionNames missing_extensions = CheckExtensionsAvailable(all_extensions);

	// if (!missing_extensions.empty()) {
	//     std::cerr << "MISSING: ";
	//     for (size_t i = 0; i < missing_extensions.size(); ++i) {
	//         std::cerr << missing_extensions[i];
	//         if (i < missing_extensions.size() - 1) {
	//             std::cerr << ", ";
	//         }
	//     }

	//     ModulePanic("Missing required instance extensions");
	// }

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
	instance_info.pNext = nullptr;

	// Allow portability devices (e.g. MoltenVK) to be shown when querying devices.
	instance_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

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
		LogError(LC_RENDER, fmt, message);
	}
	else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) {
		LogWarning(LC_RENDER, fmt, message);
	}
	else if ((message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)) {
		// Log::Info(fmt, message);
	}
	else {
		LogDebug(LC_RENDER, fmt, message);
	}

	return 0;
}

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
		LogError(LC_RENDER, "Could not create debug messenger! (err: {:s})", Util::ResultToStr(status));
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

	mAvailableExtensions.InitSize(extension_count);

	// Get the available instance extensions
	result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, mAvailableExtensions.pData);
	if (result != VK_SUCCESS) {
		throw std::runtime_error("Could not query instance extensions!");
	}

	return mAvailableExtensions;
}

void RenderBackend::SubmitPushConstantsRaw(const CommandBuffer& cmd, const Pipeline& pipeline, eShaderType shader_types,
										   const void* data, uint32 data_size) const
{
	DebugAssert(pipeline.Layout.IsValid());

	// Currently, there is nowhere in the engine that requires two separate PC buffers and therefore requires an offset.
	// As well, the small required size of a PC kind of makes this useless. For now, we will ignore this and if needed
	// there will be an updated version of this function.
	// I'm pretty sure when I was using Slang I had one shader that required this, but thats since been cacked..
	static constexpr uint32 scOffset = 0;
	vkCmdPushConstants(cmd.Get(), pipeline.Layout.Get(), ShaderUtil::ToUnderlyingType(shader_types), scOffset,
					   data_size, data);
}


void RenderBackend::SubmitImmediateUploadCmd(RenderBackend::SubmitFunc upload_func)
{
	CommandBuffer& cmd = UploadContext.ImmediateCmdBuffer;

	cmd.Record(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	upload_func(cmd);
	cmd.End();

	const VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

		.commandBufferCount = 1,
		.pCommandBuffers = &cmd.Cmd,
	};

	SpinLockContext<VkQueue> transfer_queue = GetDevice()->GetTransferQueue();

	VkTry(vkQueueSubmit(transfer_queue.Get(), 1, &submit_info, UploadContext.ImmediateUploadFence.Get()),
		  "Error submitting upload buffer");

	transfer_queue.Unlock();

	UploadContext.ImmediateUploadFence.WaitFor();
	UploadContext.ImmediateUploadFence.Reset();

	UploadContext.CmdPool.Reset();
}

void RenderBackend::SubmitUploadCmd(RenderBackend::SubmitFunc upload_func)
{
	CommandBuffer& cmd = UploadContext.CmdBuffer;
	upload_func(cmd);
}

void RenderBackend::BeginUploads() {}

void RenderBackend::SubmitUploads() {}


void RenderBackend::SubmitOneTimeCmd(RenderBackend::SubmitFunc submit_func)
{
	CommandBuffer cmd;
	cmd.Create(&GetFrame()->CmdPool);

	cmd.Record(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	submit_func(cmd);
	cmd.End();

	const VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

		.commandBufferCount = 1,
		.pCommandBuffers = &cmd.Cmd,
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

	BeginUploads();

	LightBuffer.Rewind();

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

	pDeferredRenderer->GPass.Begin(frame->CmdBuffer);
	gPipelineCache->Bind(ePipelineName::Geometry, frame->CmdBuffer);
}

void RenderBackend::PresentFrame()
{
	SubmitUploads();

	FrameData* frame = GetFrame();

	const VkPipelineStageFlags wait_stages[] = {
		VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	};

	VkSemaphore submit_semaphore = mSubmitSemaphores[mImageIndex].Get();

	VkSemaphore wait_semaphore = frame->ImageAvailable.Get();


	VkCommandBuffer cmds_to_submit[] = {
		frame->CmdBuffer.Cmd,
		// frame->TransferCmdBuffer.Cmd,
	};

	const VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,

		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &wait_semaphore,
		.pWaitDstStageMask = wait_stages,

		.commandBufferCount = std::size(cmds_to_submit),
		.pCommandBuffers = cmds_to_submit,

		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &submit_semaphore,
	};

	{
		SpinLockContext<VkQueue> graphics_queue = GetDevice()->GetGraphicsQueue();

		VkTry(vkQueueSubmit(graphics_queue.Get(), 1, &submit_info, frame->InFlight.Get()),
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
		LogError(LC_RENDER, "Error submitting present queue. Status: {:x}", static_cast<int32>(status));
	}

	bDidFrameResize = false;
}

void RenderBackend::BeginLighting()
{
	FrameData* frame = GetFrame();

	pDeferredRenderer->GPass.End();

	Target* depth_target = pDeferredRenderer->GPass.GetTarget(eImageFormat::eD32_Float, 0);
	Assert(depth_target != nullptr);
	depth_target->Image.TransitionDepthToShaderRO(frame->CmdBuffer);


	pDeferredRenderer->LightPass.Begin(frame->CmdBuffer);

	// gState->BufferOffset(ShaderType::Vertex, gRenderer->Uniforms.GetBaseOffset());
	// gState->Pipeline(&pDeferredRenderer->PlLightingDirectional);
	// gState->Apply(frame->CommandBuffer);
	// gState->Reset();


	// gPipelineCache->SetBufferOffset(0, gRenderer->LightBuffer.GetBaseOffset());
	gPipelineCache->Bind(ePipelineName::LightingDirectional, frame->CmdBuffer);

	pDeferredRenderer->DsLighting.BindWithOffset(0, frame->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
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

	pDeferredRenderer->ForwardPass.Begin(frame->CmdBuffer);
	gPipelineCache->Bind(ePipelineName::Unlit, frame->CmdBuffer);

	/*    pDeferredRenderer->RpForward.Begin(&frame->CmdBuffer, pDeferredRenderer->FbForward.Get(),
									   Slice<VkClearValue>({}, 0));*/

	// pDeferredRenderer->PlUnlit.Bind(frame->CommandBuffer);
}

void RenderBackend::DoComposition(Camera& render_cam)
{
	FrameData* frame = GetFrame();

	pDeferredRenderer->ForwardPass.End();

	// pDeferredRenderer->RpForward.End();

	pDeferredRenderer->CompPass.Begin(frame->CmdBuffer);
	gPipelineCache->Bind(ePipelineName::Composition, frame->CmdBuffer);

	pDeferredRenderer->DoCompPass(render_cam);

	pDeferredRenderer->CompPass.End();
	frame->CmdBuffer.End();

	PresentFrame();

	SpinLockContext<Queue<DeletionObject>> deletion_queue = mDeletionQueue.GetQueue();
	ProcessDeletionQueue(false, deletion_queue.Get());
	deletion_queue.Unlock();

	RequirePipelineDynamicStates();

	mInternalFrameCounter++;

	mFrameNumber = (mInternalFrameCounter % FramesInFlight);
}

void RenderBackend::RebuildToResizedWindow()
{
	bDidFrameResize = true;
	gRenderer->GetWindow()->HandleResize();
	Swapchain.Rebuild(gRenderer->GetWindow()->GetSize(), mWindowSurface);
	RebuildRenderStages();
}

eFrameResult RenderBackend::GetNextSwapchainImage(FrameData* frame)
{
	const uint64 timeout = UINT64_MAX; // TODO: change this value and handle AcquireNextImage errors correctly

	const VkResult result = vkAcquireNextImageKHR(GetDevice()->Device, Swapchain.GetSwapchain(), timeout,
												  frame->ImageAvailable.Get(), nullptr, &mImageIndex);

	if (result == VK_SUCCESS) {
		return eFrameResult::Success;
	}
	else if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		RebuildToResizedWindow();
		return eFrameResult::GraphicsOutOfDate;
	}
	else {
		LogError(LC_RENDER, "Error getting next swapchain image! Status: {:x}", static_cast<int>(result));
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

	SpinLockContext<Queue<DeletionObject>> deletion_queue = mDeletionQueue.GetQueue();

	while (!deletion_queue->IsEmpty()) {
		ProcessDeletionQueue(true, deletion_queue.Get());

		// insert a small delay to avoid the processor spinning out while
		// waiting for an object. this allows handing the core off to other threads.
		std::this_thread::sleep_for(std::chrono::nanoseconds(100));
	}

	deletion_queue.Unlock();

	gAssetManager->ShutdownDeletionQueue();


	GpuBufferPrintUndestroyed();

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
