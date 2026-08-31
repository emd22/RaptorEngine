#include "GraphicsBackend.hpp"

#include "Backend/Commands.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Synchro.hpp"
#include "Backend/Util.hpp"
#include "Constants.hpp"
#include "Engine.hpp"
#include "ImageGen.hpp"
#include "Object/ObjectManager.hpp"
#include "TiledForwardRenderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <Asset/Animation.hpp>
#include <Asset/AssetManager.hpp>
#include <Core/Assert.hpp>
#include <Core/Defines.hpp>
#include <Core/RefUtil.hpp>
#include <Core/Types.hpp>
#include <Material/MaterialManager.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Backend/ExtensionHandles.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/Limits.hpp>
#include <Renderer/PSOBuild.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/ShadowDirectional.hpp>


/* If this is defined, we will break on an error message containing this string. */
#define FX_DEBUG_BREAK_ON_ERROR_SUBSTR                                                                                 \
	"VK_FORMAT_D32_SFLOAT with tiling VK_IMAGE_TILING_OPTIMAL doesn't support VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT"

#define FX_VULKAN_DEBUG 1

// This isn't defined on some platforms/drivers.
#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif

namespace fx::renderer {

using ExtensionNames = GraphicsBackend::ExtensionNames;
using ExtensionList = GraphicsBackend::ExtensionList;

FX_SET_MODULE_NAME("RenderBackend")

ExtensionNames GraphicsBackend::CheckExtensionsAvailable(ExtensionNames& requested_extensions,
														 ExtensionList& available_extensions)
{
	if (available_extensions.IsEmpty()) {
		QueryInstanceExtensions(available_extensions);
	}

	std::vector<const char*> missing_extensions;

	for (const char* requested_name : requested_extensions) {
		bool found_extension = false;
		for (const auto& extension : available_extensions) {
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

bool GraphicsBackend::RequiresVulkanPortability(const ExtensionList& available_extensions)
{
	Assert(available_extensions.IsNotEmpty());

	for (const VkExtensionProperties& extension : available_extensions) {
		if (!strncmp(extension.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, 256)) {
			return true;
		}
	}

	return false;
}

SizedArray<VkLayerProperties> GraphicsBackend::GetAvailableValidationLayers()
{
	uint32 layer_count;
	vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

	SizedArray<VkLayerProperties> validation_layers;
	validation_layers.InitSize(layer_count);

	vkEnumerateInstanceLayerProperties(&layer_count, validation_layers.pData);

	return validation_layers;
}

VkDebugUtilsMessengerEXT CreateDebugMessenger(VkInstance instance);

void GraphicsBackend::Init(Vec2u window_size)
{
	InitVulkan();
	CreateSurfaceFromWindow();

	mDevice.Create(mInstance, mWindowSurface);

	InitGPUAllocator();
	Swapchain.Init(window_size, mWindowSurface, &mDevice);

	InitFrames();
	InitUploadContext();

	TransferSync.Create(eSemaphoreType::Timeline);

	// SpinLockContext<Queue<DeletionObject>> deletion_queue = mDeletionQueue.GetQueue();
	// deletion_queue->InitCapacity(Limits::MaxDeletionQueueItems);

	// Create final submission semaphores. Note that there is one submission semaphore
	// per Swapchain image, not frame in flight.
	mSubmitSemaphores.InitSize(Swapchain.OutputImages.Size);
	for (Semaphore& sem : mSubmitSemaphores) {
		sem.Create(eSemaphoreType::Binary);
	}

	LightBuffer.Create(scLightUniformSize, Limits::MaxActiveLights);
	BoneBuffer.Create(Limits::MaxBones * sizeof(Mat4f), 1);

	// Forward+ tiled light list buffers. These are double buffered per frame in flight, each tile's
	// contents are fully rewritten by the light culling pass every frame.
	LightGridPageSize = Limits::MaxScreenTiles * sizeof(uint32) * 2;
	LightIndexListPageSize = Limits::MaxScreenTiles * Limits::MaxLightsPerTile * sizeof(uint32);

	LightGridBuffer.Create(eGpuBufferType::StorageWithOffset, LightGridPageSize * FramesInFlight,
						   VMA_MEMORY_USAGE_GPU_ONLY);
	LightIndexListBuffer.Create(eGpuBufferType::StorageWithOffset, LightIndexListPageSize * FramesInFlight,
								VMA_MEMORY_USAGE_GPU_ONLY);


	gMaterialManager->Create();
	gObjectManager->Create();

	gShadowRenderer = new ShadowDirectional(Vec2u(2048, 2048));

	Mat4f initial_matrix = Mat4f::scIdentity;
	BoneBuffer.SetAllValues(initial_matrix.RawData, true);

	pNoiseTexture = ImageGen::Random(Vec2u(64));

	pRenderer = new TiledForwardRenderer;
	pRenderer->Create(Swapchain.Extent);

	bInitialized = true;
}

void GraphicsBackend::InitUploadContext()
{
	UploadContext.CmdPool.Create(GetDevice(), GetDevice()->mQueueFamilies.GetTransferFamily());
	UploadContext.CmdBuffer.Create(&UploadContext.CmdPool);
	UploadContext.ImmediateCmdBuffer.Create(&UploadContext.CmdPool);

	Util::SetDebugLabel("UploadImmediate", VK_OBJECT_TYPE_COMMAND_BUFFER, UploadContext.ImmediateCmdBuffer.Cmd);
	Util::SetDebugLabel("Upload", VK_OBJECT_TYPE_COMMAND_BUFFER, UploadContext.CmdBuffer.Cmd);

	UploadContext.UploadFence.Create();
	UploadContext.ImmediateUploadFence.Create();
}

void GraphicsBackend::DestroyUploadContext()
{
	UploadContext.CmdBuffer.Destroy();
	UploadContext.ImmediateCmdBuffer.Destroy();
	UploadContext.CmdPool.Destroy();

	UploadContext.UploadFence.Destroy();
	UploadContext.ImmediateUploadFence.Destroy();
}

void GraphicsBackend::InitFrames()
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

void GraphicsBackend::DestroyFrames()
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

void GraphicsBackend::RebuildRenderStages()
{
	TiledForwardRenderer* rd = pRenderer;

	Vec2u size = GetWindow()->GetSize();

	rd->ForwardPass.Rebuild(size);
	rd->Prepass.Rebuild(size);
	rd->SSAOPass.Rebuild(size);
	rd->CompPass.Rebuild(size);

	rd->DescriptorPool.Recreate();

	gDescriptorCache->RebuildAll();
}

void GraphicsBackend::InitVulkan()
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

	// This is initialized when querying for extensions
	ExtensionList available_extensions;
	ExtensionNames all_extensions = MakeInstanceExtensionList(requested_extensions, available_extensions);

#ifdef FX_VULKAN_DEBUG
	all_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	all_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

	std::cout << "Requested to load " << all_extensions.size() << " extensions...\n";

	LogDebug(LC_RENDER, "== Supported Extensions ==");
	for (const VkExtensionProperties& extension : available_extensions) {
		LogDebug(LC_RENDER, "{}", extension.extensionName);
	}

	ExtensionNames missing_extensions = CheckExtensionsAvailable(all_extensions, available_extensions);

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

static uint32 DebugMessageCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, uint32 type,
								   const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
{
	const char* message = callback_data->pMessage;
	const char* fmt = "VkValidator: {:s}";


#ifdef FX_DEBUG_BREAK_ON_ERROR_SUBSTR
	String s_msg(message);

	if (s_msg.Contains(FX_DEBUG_BREAK_ON_ERROR_SUBSTR)) {
		FX_BREAKPOINT;
	}
#endif

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

static void DestroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
	if (messenger == nullptr) {
		return;
	}

	Rx_EXT_DestroyDebugUtilsMessenger(instance, messenger, nullptr);
}

void GraphicsBackend::InitGPUAllocator()
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

void GraphicsBackend::DestroyGPUAllocator() { vmaDestroyAllocator(GpuAllocator); }

ExtensionNames GraphicsBackend::MakeInstanceExtensionList(ExtensionNames& user_requested_extensions,
														  ExtensionList& out_available_extensions)
{
	uint32 required_extension_count = 0;
	const char* const* required_extensions = SDL_Vulkan_GetInstanceExtensions(&required_extension_count);

	QueryInstanceExtensions(out_available_extensions);

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

ExtensionList& GraphicsBackend::QueryInstanceExtensions(ExtensionList& available_extensions, bool invalidate_previous)
{
	if (available_extensions.IsNotEmpty()) {
		if (invalidate_previous) {
			available_extensions.Free();
		}
		else {
			return available_extensions;
		}
	}

	// Get the count of the current extensions
	uint32_t extension_count = 0;
	VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
	if (result != VK_SUCCESS) {
		throw std::runtime_error("Could not query instance extensions!");
	}

	available_extensions.InitSize(extension_count);

	// Get the available instance extensions
	result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available_extensions.pData);
	if (result != VK_SUCCESS) {
		throw std::runtime_error("Could not query instance extensions!");
	}

	return available_extensions;
}

void GraphicsBackend::SubmitPushConstantsRaw(const CommandBuffer& cmd, const Pipeline& pipeline,
											 eShaderType shader_types, const void* data, uint32 data_size) const
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


void GraphicsBackend::SubmitImmediateUploadCmd(GraphicsBackend::SubmitFunc upload_func)
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

	UploadContext.ImmediateCmdBuffer.Reset();
}

void GraphicsBackend::SubmitUploadCmd(GraphicsBackend::SubmitFunc upload_func)
{
	CommandBuffer& cmd = UploadContext.CmdBuffer;
	upload_func(cmd);
}

void GraphicsBackend::BeginUploads() {}

void GraphicsBackend::SubmitUploads() {}


void GraphicsBackend::SubmitOneTimeCmd(GraphicsBackend::SubmitFunc submit_func)
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


eFrameResult GraphicsBackend::BeginFrame()
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

void GraphicsBackend::BeginLightCulling(Camera& render_cam) { pRenderer->DoLightCullingPass(render_cam); }

void GraphicsBackend::BeginPrepass()
{
	FrameData* frame = GetFrame();
	pRenderer->Prepass.Begin(frame->CmdBuffer);
}

void GraphicsBackend::BeginGeometry()
{
	FrameData* frame = GetFrame();

	pRenderer->ForwardPass.Begin(frame->CmdBuffer);
	// gPipelineCache->Bind(ePipelineName::Geometry, frame->CmdBuffer);

	// pDeferredRenderer->BindLightGridDescriptors(frame->CmdBuffer);

	const uint32 buffer_offsets[] = { gGraphics->GetLightGridFrameOffset(), gGraphics->GetLightIndexListFrameOffset() };

	// pLightsDescriptor->Bind(2, frame->CmdBuffer, gPipelineCache->Request(ePipelineName::GeometryNormalMaps),
	// 						Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
}

void GraphicsBackend::PresentFrame()
{
	SubmitUploads();

	FrameData* frame = GetFrame();

	const VkPipelineStageFlags wait_stages[] = {
		VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	};

	VkSemaphore submit_semaphore = mSubmitSemaphores[mImageIndex].Get();
	VkSemaphore wait_semaphores[] = {
		frame->ImageAvailable.Get(),
		TransferSync.Get(),
	};


	VkCommandBuffer cmds_to_submit[] = {
		frame->CmdBuffer.Cmd,
		// frame->TransferCmdBuffer.Cmd,
	};

	uint64_t wait_values[] = { 0, gGraphics->TransferCount.load() };

	VkTimelineSemaphoreSubmitInfo timeline_info {
		.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreValueCount = std::size(wait_values),
		.pWaitSemaphoreValues = wait_values,
		.signalSemaphoreValueCount = 0,
		.pSignalSemaphoreValues = nullptr,
	};

	const VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = &timeline_info,

		.waitSemaphoreCount = std::size(wait_semaphores),
		.pWaitSemaphores = wait_semaphores,

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


void GraphicsBackend::RenderPostProcessing(Camera& camera)
{
	FrameData* frame = GetFrame();

	pRenderer->SSAOPass.Begin(frame->CmdBuffer);

	gPipelineCache->Bind(ePipelineName::SSAO, frame->CmdBuffer);

	SSAOPushConsts consts = {};

	memcpy(consts.InvProjection, camera.InvProjectionMatrix.RawData, sizeof(float32) * 16);
	memcpy(consts.Projection, camera.ProjectionMatrix.RawData, sizeof(float32) * 16);
	memcpy(consts.View, camera.ViewMatrix.RawData, sizeof(float32) * 16);
	consts.ScreenSize[0] = static_cast<float32>(Swapchain.Extent.X);
	consts.ScreenSize[1] = static_cast<float32>(Swapchain.Extent.Y);
	consts.Radius = 0.12f;
	consts.Bias = 0.015f;

	SubmitPushConstants(frame->CmdBuffer, gPipelineCache->Request(ePipelineName::SSAO), eShaderType::Pixel, consts);

	vkCmdDraw(frame->CmdBuffer.Get(), 3, 1, 0, 0);

	pRenderer->SSAOPass.End();
}

void GraphicsBackend::DoComposition(Camera& render_cam)
{
	FrameData* frame = GetFrame();

	pRenderer->ForwardPass.End();

	RenderPostProcessing(render_cam);


	// pDeferredRenderer->UnlitPass.End();

	pRenderer->CompPass.Begin(frame->CmdBuffer);
	// gPipelineCache->Bind(ePipelineName::Composition, frame->CmdBuffer);

	pRenderer->RenderComposition(render_cam);

	pRenderer->CompPass.End();
	// SpinLockContext<Queue<DeletionObject>> deletion_queue = mDeletionQueue.GetQueue();
	// ProcessDeletionQueue(false, deletion_queue.Get());
	// deletion_queue.Unlock();
	frame->CmdBuffer.End();

	PresentFrame();


	RequirePipelineDynamicStates();

	mInternalFrameCounter++;

	mFrameNumber = (mInternalFrameCounter % FramesInFlight);
}

void GraphicsBackend::RebuildToResizedWindow()
{
	bDidFrameResize = true;
	gGraphics->GetWindow()->HandleResize();
	Swapchain.Rebuild(gGraphics->GetWindow()->GetSize(), mWindowSurface);
	RebuildRenderStages();
}

eFrameResult GraphicsBackend::GetNextSwapchainImage(FrameData* frame)
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

void GraphicsBackend::CreateSurfaceFromWindow()
{
	if (mpWindow == nullptr) {
		ModulePanic("No window attached! use RenderBackend::SelectWindow()");
	}

	bool success = SDL_Vulkan_CreateSurface(mpWindow->GetWindow(), mInstance, nullptr, &mWindowSurface);

	if (!success) {
		ModulePanic("Could not attach Vulkan instance to window! (SDL err: {})", SDL_GetError());
	}
}


void GraphicsBackend::Destroy()
{
	GetDevice()->WaitForIdle();

	DestroyUploadContext();
	DestroyFrames();

	for (Semaphore& sem : mSubmitSemaphores) {
		sem.Destroy();
	}

	LightBuffer.Destroy();
	BoneBuffer.Destroy();

	LightGridBuffer.Destroy();
	LightIndexListBuffer.Destroy();

	// SpinLockContext<Queue<DeletionObject>> deletion_queue = mDeletionQueue.GetQueue();

	// while (!deletion_queue->IsEmpty()) {
	// 	LogInfo("DELETING?");
	// 	ProcessDeletionQueue(true, deletion_queue.Get());

	// 	// insert a small delay to avoid the processor spinning out while
	// 	// waiting for an object. this allows handing the core off to other threads.
	// 	std::this_thread::sleep_for(std::chrono::nanoseconds(100));
	// }

	// deletion_queue.Unlock();

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

FrameData* GraphicsBackend::GetFrame() { return &Frames[GetFrameNumber()]; }

} // namespace fx::renderer
