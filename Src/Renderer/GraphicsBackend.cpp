#include "GraphicsBackend.hpp"

#include "Backend/Commands.hpp"
#include "Backend/Pipeline.hpp"
#include "Backend/Synchro.hpp"
#include "Backend/Util.hpp"
#include "Constants.hpp"
#include "Engine.hpp"
#include "ImageGen.hpp"
#include "Object/ObjectManager.hpp"
#include "TextRenderer.hpp"
#include "TiledForwardRenderer.hpp"

#include <Asset/Animation.hpp>
#include <Asset/AssetManager.hpp>
#include <CVar.hpp>
#include <Color.hpp>
#include <Core/Assert.hpp>
#include <Core/Defines.hpp>
#include <Core/RefUtil.hpp>
#include <Core/Types.hpp>
#include <Decal/DecalManager.hpp>
#include <Particle/ParticleSystem.hpp>
#include <Material/MaterialManager.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Backend/ExtensionHandles.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/Backend/BarrierHelper.hpp>
#include <Renderer/LightProbe.hpp>
#include <Renderer/PSOBuild.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Map/CompiledMap.hpp>
#include <Renderer/ShadowDirectional.hpp>
#include <Texture/TextureManager.hpp>
#include <World.hpp>

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

static constexpr bool scEnableValidationLayers = false;
static constexpr uint16 scHalfOne = 0x3C00;

static constexpr uint16 scFogShadowUnshadowed = 0xFFFF;
static constexpr uint32 scMaxFogShadowCells = 4096;
static constexpr uint32 scFogShadowAtlasWidth = 4096;
static constexpr uint32 scMaxFogShadowAtlasHeight = 8192;

struct FogShadowPageGpu
{
	float32 LightRect[4];
	float32 AtlasRect[4];
	float32 Depth[4];
};

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
	BoneBuffer.Create(sizeof(Mat4f), Limits::MaxBoneMatrices, eGpuBufferType::StorageWithOffset);
	FrameAlloc.Create();

	// Forward+ tiled light list buffers. These are double buffered per frame in flight, each tile's
	LightGridPageSize = Limits::MaxScreenTiles * sizeof(uint32) * 4;
	LightIndexListPageSize = Limits::MaxScreenTiles * Limits::MaxLightsPerTile * sizeof(uint32);

	LightGridBuffer.Create(eGpuBufferType::StorageWithOffset, LightGridPageSize * FramesInFlight,
						   VMA_MEMORY_USAGE_GPU_ONLY);
	LightIndexListBuffer.Create(eGpuBufferType::StorageWithOffset, LightIndexListPageSize * FramesInFlight,
								VMA_MEMORY_USAGE_GPU_ONLY);

	// Decals are written from the CPU every frame, so they get a page per frame in flight like the lights
	DecalPageSize = (Limits::MaxVisibleDecals + Limits::MaxSkinnedDecals) * sizeof(DecalGpuData);
	DecalMaskPageSize = Limits::MaxScreenTiles * Limits::DecalMaskWords * sizeof(uint32);

	DecalBuffer.Create(eGpuBufferType::StorageWithOffset, DecalPageSize * FramesInFlight, VMA_MEMORY_USAGE_CPU_ONLY,
					   eGpuBufferFlags::PersistentMapped);
	DecalMaskBuffer.Create(eGpuBufferType::StorageWithOffset, DecalMaskPageSize * FramesInFlight,
						   VMA_MEMORY_USAGE_GPU_ONLY);

	// Light probes. These only change when probes are placed, baked or loaded, and ProbeManager waits for the GPU to be
	// idle before writing them, so unlike the buffers above they have a single page shared by every frame in flight.
	ProbePageSize = Limits::MaxIrradianceProbes * sizeof(ProbeSHData);
	ProbeBuffer.Create(eGpuBufferType::StorageWithOffset, ProbePageSize, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
					   eGpuBufferFlags::PersistentMapped);

	ProbeVolumePageSize = Limits::MaxProbeVolumes * sizeof(ProbeVolumeData);
	ProbeVolumeBuffer.Create(eGpuBufferType::StorageWithOffset, ProbeVolumePageSize,
							 VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::PersistentMapped);

	ProbeGridPageSize = Limits::MaxProbeGridPoints * sizeof(uint16);
	ProbeGridBuffer.Create(eGpuBufferType::StorageWithOffset, ProbeGridPageSize, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
						   eGpuBufferFlags::PersistentMapped);


	{
		const Vec2u atlas_size(Limits::ProbeAtlasWidth, Limits::ProbeAtlasHeight);
		const uint64 atlas_bytes = static_cast<uint64>(atlas_size.X) * atlas_size.Y *
								   ImageFormatUtil::GetPixelStride(eImageFormat::RG16_UNorm);

		SizedArray<uint8> blank;
		blank.InitSize(atlas_bytes);
		memset(blank.pData, 0xFF, atlas_bytes);

		pProbeMomentsAtlas = gTextureManager->NewTexture();

		SubmitImmediateUploadCmd(
			[&](CommandBuffer& cmd)
			{
				ImageInfo info(atlas_size, eImageFormat::RG16_UNorm, 0, 1, Slice<const uint8>(blank.pData, blank.Size));

				pProbeMomentsAtlas->CreateFromData(cmd, info, eImageCreateFlags::None);
			});
	}


	{
		const uint8 neutral_lightmap[4] = { 128, 128, 128, 255 };
		const uint16 black_irradiance[4] = { 0, 0, 0, scHalfOne };

		pLightmap = CreateLightmapImage(Vec2u(1, 1), eImageFormat::RGBA8_UNorm,
										Slice<const uint8>(neutral_lightmap, sizeof(neutral_lightmap)));
		pLightmapIrradiance = CreateLightmapImage(
			Vec2u(1, 1), eImageFormat::RGBA16_Float,
			Slice<const uint8>(reinterpret_cast<const uint8*>(black_irradiance), sizeof(black_irradiance)));
	}

	{
		const uint16 unshadowed = scFogShadowUnshadowed;

		pFogShadowAtlas = CreateLightmapImage(Vec2u(1, 1), eImageFormat::D16_UNorm,
											  Slice<const uint8>(reinterpret_cast<const uint8*>(&unshadowed),
																 sizeof(unshadowed)));

		FogShadowPageBufferSize = scMaxFogShadowCells * sizeof(FogShadowPageGpu);
		FogShadowPageBuffer.Create(eGpuBufferType::StorageWithOffset, FogShadowPageBufferSize,
								   VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::PersistentMapped);

		memset(FogShadowPageBuffer.pMappedBuffer, 0, FogShadowPageBufferSize);
		FogShadowPageBuffer.FlushToGpu(0, FogShadowPageBufferSize);
	}

	ReflectionProbePageSize = Limits::MaxReflectionProbes * sizeof(ReflectionProbeData);
	ReflectionProbeBuffer.Create(eGpuBufferType::StorageWithOffset, ReflectionProbePageSize,
								 VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, eGpuBufferFlags::PersistentMapped);

	{
		if (!mDevice.bSupportsCubeArrays) {
			LogError("The GPU has no cubemap arrays, which the reflection probes need");
		}

		pReflectionProbes = gTextureManager->NewTexture();
		pReflectionProbes->Create(eImageType::CubemapArray,
								  Vec2u(Limits::ReflectionProbeSize, Limits::ReflectionProbeSize),
								  Limits::ReflectionProbeMips, eImageFormat::RGBA16_Float,
								  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, eImageAspectFlag::Color,
								  eImageCreateFlags::None, Limits::MaxReflectionProbes);

		SubmitImmediateUploadCmd(
			[&](CommandBuffer& cmd)
			{
				BarrierHelper::ImageLayoutTransition(pReflectionProbes, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd,
													 0, Limits::ReflectionProbeMips);
			});
	}

	gMaterialManager->Create();
	gObjectManager->Create();

	// Upload the default probes now that the probe buffers exist
	gProbeManager->Create();

	// Starts loading the decal atlas, which the renderer binds once it's in
	gDecalManager->Create();

	gParticleSystem->Create();

	gShadowAtlas = new ShadowAtlas;
	gShadowRenderer = new ShadowDirectional;

	pNoiseTexture = ImageGen::Random(Vec2u(64));
	pDfgLut = ImageGen::DfgLut(Limits::DfgLutSize);

	pRenderer = new TiledForwardRenderer;
	pRenderer->Create(Swapchain.Extent);

	bInitialized = true;
}

void GraphicsBackend::InitUploadContext()
{
	UploadContext.CmdPool.Create(GetDevice(), GetDevice()->mQueueFamilies.GetTransferFamily());
	UploadContext.CmdBuffer.Create(&UploadContext.CmdPool);
	UploadContext.ImmediateCmdPool.Create(GetDevice(), GetDevice()->mQueueFamilies.GetTransferFamily());
	UploadContext.ImmediateCmdBuffer.Create(&UploadContext.ImmediateCmdPool);

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
	UploadContext.ImmediateCmdPool.Destroy();

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

	Profiler.Create(device, graphics_family);
}

void GraphicsBackend::DestroyFrames()
{
	{
		SpinLockContext<VkQueue> graphics_queue = GetDevice()->GetGraphicsQueue();
		vkQueueWaitIdle(graphics_queue.Get());
	}

	// Nothing that uses the query pools is in flight now
	Profiler.Destroy();

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

	// The forward pass shares the prepass's depth image, so the prepass has to be rebuilt first
	rd->Prepass.Rebuild(size);
	rd->ForwardPass.Rebuild(size);
	rd->ParticlePass.Rebuild(size);
	rd->FogPass.Rebuild(size);
	rd->FogTemporalPass.Rebuild(size);
	rd->FogDenoisePass.Rebuild(size);
	rd->FogApplyPass.Rebuild(size);
	rd->SSAOPass.Rebuild(size);
	rd->SSAODenoisePass.Rebuild(size);
	rd->SSAOBlurPass.Rebuild(size);
	rd->CompPass.Rebuild(size);

	gTextRenderer->Resize();

	rd->DescriptorPool.Recreate();

	gDescriptorCache->RebuildAll();
}

void GraphicsBackend::InitVulkan()
{
	const char* app_name = "RaptorEngine";
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

	StackArray<const char*, 1> requested_validation_layers = {
		// "VK_LAYER_KHRONOS_shader_object",
	};


	if (scEnableValidationLayers) {
		requested_validation_layers.Insert("VK_LAYER_KHRONOS_validation");
	}

	VkInstanceCreateInfo instance_info = {};
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &app_info;
	instance_info.ppEnabledExtensionNames = all_extensions.data();
	instance_info.enabledExtensionCount = static_cast<uint32_t>(all_extensions.size());
	instance_info.ppEnabledLayerNames = requested_validation_layers.pData;
	instance_info.enabledLayerCount = static_cast<uint32_t>(requested_validation_layers.Size);
	instance_info.pNext = nullptr;

	// Allow portability devices (e.g. MoltenVK) to be shown when querying devices. The flag is only
	// valid alongside the portability enumeration extension.
	if (RequiresVulkanPortability(available_extensions)) {
		all_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		instance_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		instance_info.ppEnabledExtensionNames = all_extensions.data();
		instance_info.enabledExtensionCount = static_cast<uint32_t>(all_extensions.size());
	}

	VkResult result = vkCreateInstance(&instance_info, nullptr, &mInstance);

	if (result != VK_SUCCESS) {
		ModulePanicVulkan("Could not create vulkan instance!", result);
	}

#ifdef FX_VULKAN_DEBUG

	if (scEnableValidationLayers) {
		mDebugMessenger = CreateDebugMessenger(mInstance);
		if (!mDebugMessenger) {
			ModulePanic("Could not create debug messenger");
		}
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
	const char* const* required_extensions = Window::GetRequiredInstanceExtensions(&required_extension_count);

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
	std::lock_guard<std::mutex> lock(UploadContext.ImmediateMutex);

	struct InFlightScope
	{
		explicit InFlightScope(std::atomic_uint32_t& count) : Count(count) { ++Count; }
		~InFlightScope() { --Count; }

		std::atomic_uint32_t& Count;
	} in_flight { UploadContext.ImmediateUploadsInFlight };

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

	// The fence is created signaled; it must be reset before each submit.
	UploadContext.ImmediateUploadFence.Reset();

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
	BoneBuffer.Rewind();
	FrameAlloc.Rewind();

	frame->InFlight.WaitFor();
	frame->InFlight.Reset();

	eFrameResult result = GetNextSwapchainImage(frame);
	if (result != eFrameResult::Success) {
		return result;
	}

	return eFrameResult::Success;
}

bool GraphicsBackend::UploadFogShadow(const CompiledMapFogShadow& shadow)
{
	bHasBakedFogShadow = false;

	const uint64 cell_count = static_cast<uint64>(shadow.CellCount[0]) * shadow.CellCount[1];

	if (shadow.IsEmpty() || cell_count > scMaxFogShadowCells) {
		if (cell_count > scMaxFogShadowCells) {
			LogWarning(LC_RENDER, "The fog shadow covers {} pages, more than the {} that fit", cell_count,
					   scMaxFogShadowCells);
		}

		return false;
	}

	SizedArray<uint32> order;
	order.InitSize(shadow.Pages.Size);

	uint32 widest = 1;
	uint64 total_width = 0;

	for (uint32 i = 0; i < shadow.Pages.Size; i++) {
		order[i] = i;

		const CompiledMapFogShadowLevel& detail = shadow.Pages[i].Levels[0];
		widest = std::max(widest, detail.Width);
		total_width += detail.Width;
	}

	std::sort(order.begin(), order.end(), [&](uint32 a, uint32 b)
			  { return shadow.Pages[a].Levels[0].Height > shadow.Pages[b].Levels[0].Height; });

	const uint32 atlas_width = std::max(widest, static_cast<uint32>(std::min<uint64>(total_width,
																						scFogShadowAtlasWidth)));

	SizedArray<Vec2u> placements;
	placements.InitSize(shadow.Pages.Size);

	uint32 cursor_x = 0;
	uint32 shelf_y = 0;
	uint32 shelf_height = 0;

	for (const uint32 page : order) {
		const CompiledMapFogShadowLevel& detail = shadow.Pages[page].Levels[0];

		if (cursor_x + detail.Width > atlas_width) {
			cursor_x = 0;
			shelf_y += shelf_height;
			shelf_height = 0;
		}

		placements[page] = Vec2u(cursor_x, shelf_y);

		cursor_x += detail.Width;
		shelf_height = std::max(shelf_height, detail.Height);
	}

	const uint32 atlas_height = std::max(shelf_y + shelf_height, 1U);

	if (atlas_height > scMaxFogShadowAtlasHeight) {
		LogWarning(LC_RENDER, "The fog shadow pages need a {}x{} atlas, more than {} high", atlas_width, atlas_height,
				   scMaxFogShadowAtlasHeight);
		return false;
	}

	SizedArray<uint16> texels;
	texels.InitSize(static_cast<size_t>(atlas_width) * atlas_height);
	std::fill(texels.begin(), texels.end(), scFogShadowUnshadowed);

	SizedArray<FogShadowPageGpu> cells;
	cells.InitSize(cell_count);
	memset(cells.pData, 0, cells.GetSizeInBytes());

	for (uint32 i = 0; i < shadow.Pages.Size; i++) {
		const CompiledMapFogShadowPage& page = shadow.Pages[i];
		const CompiledMapFogShadowLevel& detail = page.Levels[0];
		const Vec2u position = placements[i];

		for (uint32 row = 0; row < detail.Height; row++) {
			memcpy(texels.pData + static_cast<size_t>(position.Y + row) * atlas_width + position.X,
				   detail.Depths.pData + static_cast<size_t>(row) * detail.Width, detail.Width * sizeof(uint16));
		}

		const uint64 cell = static_cast<uint64>(page.CellZ - shadow.CellMin[1]) * shadow.CellCount[0] +
							static_cast<uint64>(page.CellX - shadow.CellMin[0]);

		FogShadowPageGpu& gpu = cells[cell];
		gpu.LightRect[0] = page.LightMin[0];
		gpu.LightRect[1] = page.LightMin[1];
		gpu.LightRect[2] = 1.0f / page.LightSize[0];
		gpu.LightRect[3] = 1.0f / page.LightSize[1];
		gpu.AtlasRect[0] = static_cast<float32>(detail.Width) / atlas_width;
		gpu.AtlasRect[1] = static_cast<float32>(detail.Height) / atlas_height;
		gpu.AtlasRect[2] = static_cast<float32>(position.X) / atlas_width;
		gpu.AtlasRect[3] = static_cast<float32>(position.Y) / atlas_height;
		gpu.Depth[0] = page.DepthMin;
		gpu.Depth[1] = 1.0f / std::max(page.DepthMax - page.DepthMin, 1e-3f);
		gpu.Depth[2] = 1.0f;
	}

	mDevice.WaitForIdle();

	pFogShadowAtlas->ImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	SubmitImmediateUploadCmd(
		[&](CommandBuffer& cmd)
		{
			ImageInfo info(Vec2u(atlas_width, atlas_height), eImageFormat::D16_UNorm, 0, 1,
						   Slice<const uint8>(reinterpret_cast<const uint8*>(texels.pData), texels.GetSizeInBytes()));

			pFogShadowAtlas->CreateFromData(cmd, info, eImageCreateFlags::None);
		});

	gDescriptorCache->RebuildUsingImage(pFogShadowAtlas);

	memcpy(FogShadowPageBuffer.pMappedBuffer, cells.pData, cells.GetSizeInBytes());
	FogShadowPageBuffer.FlushToGpu(0, static_cast<uint32>(cells.GetSizeInBytes()));

	for (uint32 axis = 0; axis < 3; axis++) {
		FogShadow.Right[axis] = shadow.Right[axis];
		FogShadow.Up[axis] = shadow.Up[axis];
		FogShadow.Forward[axis] = shadow.Forward[axis];
	}

	FogShadow.PageSize = shadow.PageSize;
	FogShadow.CellMin[0] = shadow.CellMin[0];
	FogShadow.CellMin[1] = shadow.CellMin[1];
	FogShadow.CellCount[0] = shadow.CellCount[0];
	FogShadow.CellCount[1] = shadow.CellCount[1];
	FogShadow.InvAtlasSize[0] = 1.0f / atlas_width;
	FogShadow.InvAtlasSize[1] = 1.0f / atlas_height;

	bHasBakedFogShadow = true;

	LogInfo("Loaded fog shadow pages; {} of {} cells, {}x{} atlas", shadow.Pages.Size, cell_count, atlas_width,
			atlas_height);

	return true;
}

Image* GraphicsBackend::CreateLightmapImage(const Vec2u& size, eImageFormat format, const Slice<const uint8>& texels)
{
	Image* image = gTextureManager->NewTexture();

	SubmitImmediateUploadCmd(
		[&](CommandBuffer& cmd)
		{
			ImageInfo info(size, format, 0, 1, texels);

			image->CreateFromData(cmd, info, eImageCreateFlags::None);
		});

	return image;
}

bool GraphicsBackend::UploadLightmap(const Vec2u& size, const uint8* sun_visibility, const uint16* irradiance,
									 const uint8* directions)
{
	if (pRenderer == nullptr || sun_visibility == nullptr || size.X == 0 || size.Y == 0 ||
		size.X > Limits::LightmapSize || size.Y > Limits::LightmapSize) {
		return false;
	}

	const uint64 texel_count = static_cast<uint64>(size.X) * size.Y;

	SizedArray<uint8> lightmap_texels;
	lightmap_texels.InitSize(texel_count * 4);

	for (uint64 texel = 0; texel < texel_count; texel++) {
		for (uint32 channel = 0; channel < 3; channel++) {
			uint8 direction = 128;

			if (directions != nullptr) {
				direction = directions[texel * 3 + channel];
			}

			lightmap_texels[texel * 4 + channel] = direction;
		}

		lightmap_texels[texel * 4 + 3] = sun_visibility[texel];
	}

	Vec2u irradiance_size(1, 1);

	if (irradiance != nullptr) {
		irradiance_size = size;
	}

	const uint64 irradiance_count = static_cast<uint64>(irradiance_size.X) * irradiance_size.Y;

	SizedArray<uint16> irradiance_texels;
	irradiance_texels.InitSize(irradiance_count * 4);

	for (uint64 texel = 0; texel < irradiance_count; texel++) {
		for (uint32 channel = 0; channel < 3; channel++) {
			uint16 value = 0;

			if (irradiance != nullptr) {
				value = irradiance[texel * 3 + channel];
			}

			irradiance_texels[texel * 4 + channel] = value;
		}

		irradiance_texels[texel * 4 + 3] = scHalfOne;
	}

	mDevice.WaitForIdle();

	Image* old_lightmap = pLightmap;
	Image* old_irradiance = pLightmapIrradiance;

	pLightmap = CreateLightmapImage(size, eImageFormat::RGBA8_UNorm,
									Slice<const uint8>(lightmap_texels.pData, lightmap_texels.Size));
	pLightmapIrradiance = CreateLightmapImage(
		irradiance_size, eImageFormat::RGBA16_Float,
		Slice<const uint8>(reinterpret_cast<const uint8*>(irradiance_texels.pData), irradiance_texels.Size * 2));

	pRenderer->RebuildPersistentDescriptor();

	gTextureManager->DestroyTexture(old_lightmap->ID);
	gTextureManager->DestroyTexture(old_irradiance->ID);

	return true;
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

	// The tiled light list offsets are applied with the persistent set 0 in World::ExecuteRenderList()
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


static constexpr float32 scDefaultGTAORadius = 0.5f;
static constexpr float32 scDefaultGTAORadiusMultiplier = 1.457f;
static constexpr float32 scDefaultGTAOFalloffRange = 0.615f;
static constexpr float32 scDefaultGTAOSampleDistributionPower = 2.0f;
static constexpr float32 scDefaultGTAOThinOccluderCompensation = 0.0f;
static constexpr float32 scDefaultGTAOFinalValuePower = 2.2f;
static constexpr float32 scDefaultGTAODenoiseBlurBeta = 1.2f;

void GraphicsBackend::RenderEarlyFrameEffects(Camera& camera)
{
	FrameData* frame = GetFrame();

	pRenderer->SSAOPass.Begin(frame->CmdBuffer);

	Target* target = pRenderer->SSAOPass.GetTarget(eImageFormat::RG16_UNorm);
	Assert(target != nullptr);

	const float32 target_width = static_cast<float32>(target->Image.Info.Size.X);
	const float32 target_height = static_cast<float32>(target->Image.Info.Size.Y);

	SSAOConsts consts = {
		.RenderSize = { target_width, target_height },
	};

	memcpy(consts.InvProjection, camera.InvProjectionMatrix.RawData, sizeof(float32) * 16);
	memcpy(consts.Projection, camera.ProjectionMatrix.RawData, sizeof(float32) * 16);
	memcpy(consts.View, camera.ViewMatrix.RawData, sizeof(float32) * 16);

	consts.EffectRadius = gCVars->Get("r_gtao_radius", scDefaultGTAORadius) *
						  gCVars->Get("r_gtao_radius_multiplier", scDefaultGTAORadiusMultiplier);
	consts.FalloffRange = gCVars->Get("r_gtao_falloff_range", scDefaultGTAOFalloffRange);
	consts.SampleDistributionPower =
		gCVars->Get("r_gtao_sample_distribution_power", scDefaultGTAOSampleDistributionPower);
	consts.ThinOccluderCompensation =
		gCVars->Get("r_gtao_thin_occluder_compensation", scDefaultGTAOThinOccluderCompensation);
	consts.FinalValuePower = gCVars->Get("r_gtao_final_value_power", scDefaultGTAOFinalValuePower);

	gPipelineCache->SetFrameData(FrameAlloc.Push(consts));
	gPipelineCache->Bind(ePipelineName::SSAO, frame->CmdBuffer);

	vkCmdDraw(frame->CmdBuffer.Get(), 3, 1, 0, 0);

	pRenderer->SSAOPass.End();

	const SSAOBlurConsts blur_consts = {
		.TexelSize = { 1.0f / target_width, 1.0f / target_height },
		.BlurBeta = gCVars->Get("r_gtao_denoise_blur_beta", scDefaultGTAODenoiseBlurBeta),
	};

	const uint32 blur_consts_offset = FrameAlloc.Push(blur_consts);

	pRenderer->SSAODenoisePass.Begin(frame->CmdBuffer);

	gPipelineCache->SetFrameData(blur_consts_offset);
	gPipelineCache->Bind(ePipelineName::SSAODenoise, frame->CmdBuffer);

	vkCmdDraw(frame->CmdBuffer.Get(), 3, 1, 0, 0);

	pRenderer->SSAODenoisePass.End();

	pRenderer->SSAOBlurPass.Begin(frame->CmdBuffer);

	gPipelineCache->SetFrameData(blur_consts_offset);
	gPipelineCache->Bind(ePipelineName::SSAOBlur, frame->CmdBuffer);

	vkCmdDraw(frame->CmdBuffer.Get(), 3, 1, 0, 0);

	pRenderer->SSAOBlurPass.End();
}

static constexpr int64 scDefaultFogMode = static_cast<int64>(eFogMode::Volumetric);
static constexpr int64 scDefaultFogSteps = 64;
static constexpr int64 scMaxFogSteps = 256;
static constexpr float32 scDefaultFogDistance = 100.0f;
static constexpr float32 scMaxFogAnisotropy = 0.95f;
static constexpr float32 scMinFogFalloff = 1e-4f;
static constexpr float32 scFogHistoryBlend = 0.1f;
static constexpr float32 scFroxelHistoryBlend = 0.08f;
static constexpr float64 scFogJitterSequenceStep = 0.6180339887498949;
static constexpr float32 scBakedFogShadowBias = 0.02f;
static constexpr float32 scBakedFogShadowPcfRadius = 0.05f;

eFogMode GraphicsBackend::GetFogMode() const
{
	const FogSettings& fog = gWorld->Fog;

	if (!fog.bEnabled || (fog.Density <= 0.0f && fog.Haze <= 0.0f)) {
		return eFogMode::Off;
	}

	const int64 mode = gCVars->Get("r_fog", scDefaultFogMode);

	if (mode >= static_cast<int64>(eFogMode::Froxel)) {
		return eFogMode::Froxel;
	}

	if (mode == static_cast<int64>(eFogMode::Volumetric)) {
		return eFogMode::Volumetric;
	}

	if (mode == static_cast<int64>(eFogMode::Fast)) {
		return eFogMode::Fast;
	}

	return eFogMode::Off;
}

void GraphicsBackend::RenderFog(Camera& camera)
{
	const eFogMode mode = GetFogMode();

	if (mode == eFogMode::Off) {
		return;
	}

	CommandBuffer& cmd = GetFrame()->CmdBuffer;

	const FogSettings& fog = gWorld->Fog;

	const float32* projection = camera.ProjectionMatrix.RawData;
	const float32* inv_view = camera.InvViewMatrix.RawData;

	const float32 depth_a = static_cast<float32>(1.0 - static_cast<float64>(projection[10]));
	const float32 depth_b = -projection[14];

	FogConsts consts {};

	static constexpr float32 scCornerNdcX[] = { -1.0f, 1.0f, -1.0f, 1.0f };
	static constexpr float32 scCornerNdcY[] = { -1.0f, -1.0f, 1.0f, 1.0f };

	for (uint32 corner = 0; corner < 4; corner++) {
		const float32 view_x = scCornerNdcX[corner] / projection[0];
		const float32 view_y = scCornerNdcY[corner] / projection[5];

		for (uint32 axis = 0; axis < 3; axis++) {
			consts.Rays[corner][axis] = (view_x * inv_view[axis]) + (view_y * inv_view[4 + axis]) + inv_view[8 + axis];
		}
	}

	consts.DepthA = depth_a;
	consts.DepthB = depth_b;
	consts.MarchDistance = std::max(gCVars->Get("r_fog_distance", scDefaultFogDistance), 0.1f);
	consts.PreExposure = PreExposure;

	const Vec3f eye(inv_view[12], inv_view[13], inv_view[14]);
	Vec3f origin = eye;

	const Player& player = gWorld->Player;

	if (player.pCamera.IsValid() && &(*player.pCamera) == &camera) {
		origin = player.GetEyePosition();
	}

	const float32 falloff = std::max(fog.Falloff, scMinFogFalloff);
	const float32 origin_exponent = std::clamp(-falloff * (origin.Y - fog.Height), -80.0f, 80.0f);

	consts.OriginAndDensity[0] = origin.X;
	consts.OriginAndDensity[1] = origin.Y;
	consts.OriginAndDensity[2] = origin.Z;
	consts.OriginAndDensity[3] = std::max(fog.Density, 0.0f) * std::exp(origin_exponent);

	fog.Albedo.GetLinearRGB(consts.AlbedoAndFalloff);
	consts.AlbedoAndFalloff[3] = falloff;

	consts.CameraOffsetAndHaze[0] = eye.X - origin.X;
	consts.CameraOffsetAndHaze[1] = eye.Y - origin.Y;
	consts.CameraOffsetAndHaze[2] = eye.Z - origin.Z;
	consts.CameraOffsetAndHaze[3] = std::max(fog.Haze, 0.0f);

	consts.ScreenSize[0] = static_cast<float32>(Swapchain.Extent.X);
	consts.ScreenSize[1] = static_cast<float32>(Swapchain.Extent.Y);

	consts.Anisotropy = std::clamp(fog.Anisotropy, -scMaxFogAnisotropy, scMaxFogAnisotropy);
	consts.AmbientScale = std::max(fog.AmbientScale, 0.0f);

	consts.TileColumns = pRenderer->GetLightTileColumns();
	consts.TileRows = pRenderer->GetLightTileRows();
	consts.StepCount = static_cast<uint32>(
		std::clamp(gCVars->Get("r_fog_steps", scDefaultFogSteps), int64(1), scMaxFogSteps));

	const uint32 frame_counter = mInternalFrameCounter.load();
	const bool temporal = gCVars->Get("r_fog_temporal", int64(1)) != 0;

	consts.JitterOffset = 0.5f;
	consts.NoiseShift = 0;
	consts.JitterAmount = 0.0f;
	consts.HistoryBlend = 0.0f;

	if (temporal) {
		const float64 sequence = static_cast<float64>(frame_counter) * scFogJitterSequenceStep;
		consts.JitterOffset = static_cast<float32>(sequence - std::floor(sequence));
		consts.NoiseShift = frame_counter & 15U;
		consts.JitterAmount = 1.0f;
	}

	memcpy(consts.PrevCameraMatrix, mFogPrevCameraMatrix.RawData, sizeof(consts.PrevCameraMatrix));

	if (bBakedSunActive) {
		consts.Flags |= static_cast<uint32>(eFogFlags::ProbeSunVisibility);
		consts.Flags |= static_cast<uint32>(eFogFlags::NearSunDynamicOnly);
	}

	if (bBakedSunActive && bHasBakedFogShadow) {
		consts.Flags |= static_cast<uint32>(eFogFlags::BakedSunShadow);

		for (uint32 axis = 0; axis < 3; axis++) {
			consts.BakedShadowRight[axis] = FogShadow.Right[axis];
			consts.BakedShadowUp[axis] = FogShadow.Up[axis];
			consts.BakedShadowForward[axis] = FogShadow.Forward[axis];
		}

		consts.BakedShadowRight[3] = FogShadow.PageSize;
		consts.BakedShadowUp[3] = scBakedFogShadowBias;
		consts.BakedShadowForward[3] = scBakedFogShadowPcfRadius;

		consts.BakedShadowGrid[0] = static_cast<float32>(FogShadow.CellMin[0]);
		consts.BakedShadowGrid[1] = static_cast<float32>(FogShadow.CellMin[1]);
		consts.BakedShadowGrid[2] = static_cast<float32>(FogShadow.CellCount[0]);
		consts.BakedShadowGrid[3] = static_cast<float32>(FogShadow.CellCount[1]);

		consts.BakedShadowAtlas[0] = FogShadow.InvAtlasSize[0];
		consts.BakedShadowAtlas[1] = FogShadow.InvAtlasSize[1];
	}

	if (bFarSunShadowActive) {
		consts.Flags |= static_cast<uint32>(eFogFlags::FarSunShadow);

		memcpy(consts.FarSunMatrix, gShadowRenderer->FarShadowCamera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData,
			   sizeof(consts.FarSunMatrix));
		gShadowAtlas->GetRegionUVTransform(gShadowAtlas->GetFarDirectionalRegion(), consts.FarSunAtlasRect);
	}
	else {
		memcpy(consts.FarSunMatrix, Mat4f::scIdentity.RawData, sizeof(consts.FarSunMatrix));
	}

	if (mode == eFogMode::Froxel) {
		Image& history = pRenderer->FroxelHistory;

		bool history_valid = temporal && (mFroxelHistoryFrame + 1 == frame_counter);

		if (history.ImageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			BarrierHelper::ImageBeginComputeRead(cmd, &history);
			history_valid = false;
		}

		if (history_valid) {
			consts.HistoryBlend = scFroxelHistoryBlend;
		}

		RenderFroxelFog(cmd, FrameAlloc.Push(consts));

		mFogPrevCameraMatrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);
		mFroxelHistoryFrame = frame_counter;

		return;
	}

	ePipelineName march_pipeline = ePipelineName::FogFast;

	if (mode == eFogMode::Volumetric) {
		march_pipeline = ePipelineName::FogVolumetric;
	}

	pRenderer->FogPass.Begin(cmd);

	gPipelineCache->AddBufferOffset(0, LightBuffer.GetBaseOffset());
	gPipelineCache->AddBufferOffset(0, GetLightGridFrameOffset());
	gPipelineCache->AddBufferOffset(0, GetLightIndexListFrameOffset());
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->SetFrameData(FrameAlloc.Push(consts));
	gPipelineCache->Bind(march_pipeline, cmd);

	vkCmdDraw(cmd.Get(), 3, 1, 0, 0);

	pRenderer->FogPass.End();

	bool history_valid = temporal && (mFogHistoryFrame + 1 == frame_counter);

	Image* history_images[] = {
		&pRenderer->FogDenoisePass.GetTarget(eImageFormat::RGBA16_Float, 1)->GetImage(),
		&pRenderer->FogDenoisePass.GetTarget(eImageFormat::R32_SFloat)->GetImage(),
	};

	for (Image* history : history_images) {
		if (history->ImageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			BarrierHelper::ImageLayoutTransition(history, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0, 1);
			history_valid = false;
		}
	}

	FogTemporalConsts temporal_consts {};

	memcpy(temporal_consts.PrevCameraMatrix, mFogPrevCameraMatrix.RawData, sizeof(temporal_consts.PrevCameraMatrix));
	memcpy(temporal_consts.Rays, consts.Rays, sizeof(temporal_consts.Rays));

	temporal_consts.EyeAndBlend[0] = eye.X;
	temporal_consts.EyeAndBlend[1] = eye.Y;
	temporal_consts.EyeAndBlend[2] = eye.Z;
	temporal_consts.EyeAndBlend[3] = scFogHistoryBlend;
	temporal_consts.DepthParams[0] = depth_a;
	temporal_consts.DepthParams[1] = depth_b;
	temporal_consts.HistoryValid = static_cast<uint32>(history_valid);

	pRenderer->FogTemporalPass.Begin(cmd);

	gPipelineCache->SetFrameData(FrameAlloc.Push(temporal_consts));
	gPipelineCache->Bind(ePipelineName::FogTemporal, cmd);

	vkCmdDraw(cmd.Get(), 3, 1, 0, 0);

	pRenderer->FogTemporalPass.End();

	mFogPrevCameraMatrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);
	mFogHistoryFrame = frame_counter;

	const FogResolveConsts resolve_consts {
		.DepthParams = { depth_a, depth_b },
	};

	const uint32 resolve_consts_offset = FrameAlloc.Push(resolve_consts);

	pRenderer->FogDenoisePass.Begin(cmd);

	gPipelineCache->SetFrameData(resolve_consts_offset);
	gPipelineCache->Bind(ePipelineName::FogDenoise, cmd);

	vkCmdDraw(cmd.Get(), 3, 1, 0, 0);

	pRenderer->FogDenoisePass.End();

	pRenderer->FogApplyPass.Begin(cmd);

	gPipelineCache->SetFrameData(resolve_consts_offset);
	gPipelineCache->Bind(ePipelineName::FogApply, cmd);

	vkCmdDraw(cmd.Get(), 3, 1, 0, 0);

	pRenderer->FogApplyPass.End();
}

void GraphicsBackend::RenderFroxelFog(CommandBuffer& cmd, uint32 consts_offset)
{
	static constexpr uint32 scGroupSize = 8;

	const uint32 group_columns = (scFroxelGridX + scGroupSize - 1) / scGroupSize;
	const uint32 group_rows = (scFroxelGridY + scGroupSize - 1) / scGroupSize;

	BarrierHelper::ComputeReadAfterAll(cmd);
	BarrierHelper::ImageBeginComputeWrite(cmd, &pRenderer->FroxelLighting);
	BarrierHelper::ImageBeginComputeWrite(cmd, &pRenderer->FroxelIntegrated);
	BarrierHelper::ImageBeginComputeWrite(cmd, &pRenderer->FroxelSurface);

	gPipelineCache->AddBufferOffset(0, LightBuffer.GetBaseOffset());
	gPipelineCache->AddBufferOffset(0, GetLightGridFrameOffset());
	gPipelineCache->AddBufferOffset(0, GetLightIndexListFrameOffset());
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->SetFrameData(consts_offset);
	gPipelineCache->Bind(ePipelineName::FogFroxelInject, cmd);

	vkCmdDispatch(cmd.Get(), group_columns, group_rows, scFroxelGridZ + 1);

	BarrierHelper::ImageComputeWriteToRead(cmd, &pRenderer->FroxelLighting);
	BarrierHelper::ImageComputeWriteToRead(cmd, &pRenderer->FroxelSurface);

	gPipelineCache->SetFrameData(consts_offset);
	gPipelineCache->Bind(ePipelineName::FogFroxelIntegrate, cmd);

	vkCmdDispatch(cmd.Get(), group_columns, group_rows, 1);

	BarrierHelper::ImageComputeWriteToRead(cmd, &pRenderer->FroxelIntegrated);

	pRenderer->FogApplyPass.Begin(cmd);

	gPipelineCache->SetFrameData(consts_offset);
	gPipelineCache->Bind(ePipelineName::FogFroxelApply, cmd);

	vkCmdDraw(cmd.Get(), 3, 1, 0, 0);

	pRenderer->FogApplyPass.End();

	BarrierHelper::ImageCopyToComputeHistory(cmd, &pRenderer->FroxelLighting, &pRenderer->FroxelHistory);
}

void GraphicsBackend::DoComposition(Camera& render_cam)
{
	FrameData* frame = GetFrame();


	pRenderer->ForwardPass.End();
	MarkGpu(eGpuMarker::Forward);

	gParticleSystem->Render(frame->CmdBuffer, render_cam, pRenderer->ParticlePass);
	MarkGpu(eGpuMarker::Particles);

	RenderFog(render_cam);
	MarkGpu(eGpuMarker::Fog);

	// Probe capture bake faces render here: the main forward pass is done, so
	// the capture can reuse its pipelines + light grid page before composition.
	if (gProbeManager->IsCapturePending()) {
		gWorld->RenderProbeCapture();
		MarkGpu(eGpuMarker::ProbeCapture);
	}

	// pDeferredRenderer->UnlitPass.End();

	pRenderer->CompPass.Begin(frame->CmdBuffer);
	// gPipelineCache->Bind(ePipelineName::Composition, frame->CmdBuffer);

	pRenderer->RenderComposition(render_cam);

	pRenderer->CompPass.End();
	MarkGpu(eGpuMarker::Composition);
	// SpinLockContext<Queue<DeletionObject>> deletion_queue = mDeletionQueue.GetQueue();
	// ProcessDeletionQueue(false, deletion_queue.Get());
	// deletion_queue.Unlock();
	FrameAlloc.Flush();
	frame->CmdBuffer.End();

	PresentFrame();

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

	if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
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

	mWindowSurface = mpWindow->CreateSurface(mInstance);
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
	FrameAlloc.Destroy();

	LightGridBuffer.Destroy();
	LightIndexListBuffer.Destroy();
	DecalBuffer.Destroy();
	DecalMaskBuffer.Destroy();
	ProbeBuffer.Destroy();
	ProbeVolumeBuffer.Destroy();
	ProbeGridBuffer.Destroy();
	ReflectionProbeBuffer.Destroy();
	FogShadowPageBuffer.Destroy();

	gAssetManager->ShutdownDeletionQueue();

	TransferSync.Destroy();

	GpuBufferPrintUndestroyed();

	GetDevice()->WaitForIdle();
	Swapchain.Destroy();

	DestroyGPUAllocator();

	if (mWindowSurface) {
		vkDestroySurfaceKHR(mInstance, mWindowSurface, nullptr);
	}

	GetDevice()->Destroy();

	if (scEnableValidationLayers) {
		DestroyDebugMessenger(mInstance, mDebugMessenger);
	}

	vkDestroyInstance(mInstance, nullptr);

	bInitialized = false;
}

FrameData* GraphicsBackend::GetFrame() { return &Frames[GetFrameNumber()]; }

} // namespace fx::renderer
