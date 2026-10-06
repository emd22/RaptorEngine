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
#include <Material/MaterialManager.hpp>
#include <Renderer/Backend/BarrierHelper.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/LightProbe.hpp>
#include <Renderer/PSOBuild.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/ShadowDirectional.hpp>
#include <Texture/TextureManager.hpp>
#include <Util/RustInterop.hpp>
#include <World.hpp>

#define FX_VULKAN_DEBUG 1

// This isn't defined on some platforms/drivers.
#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif

namespace fx::renderer {

using ExtensionNames = GraphicsBackend::ExtensionNames;
using ExtensionList = GraphicsBackend::ExtensionList;

static constexpr bool scEnableValidationLayers = false;

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

void GraphicsBackend::Init(Vec2u window_size)
{
	InitVulkan();
	CreateSurfaceFromWindow();

	mDevice.Create(mpGpuInstance, mWindowSurface);

	InitGPUAllocator();
	Swapchain.Init(window_size, mWindowSurface, &mDevice);

	InitFrames();
	InitUploadContext();

	// SpinLockContext<Queue<DeletionObject>> deletion_queue = mDeletionQueue.GetQueue();
	// deletion_queue->InitCapacity(Limits::MaxDeletionQueueItems);

	LightBuffer.Create(scLightUniformSize, Limits::MaxActiveLights);
	BoneBuffer.Create(sizeof(Mat4f), Limits::MaxBoneMatrices, eGpuBufferType::StorageWithOffset);

	// Forward+ tiled light list buffers. These are double buffered per frame in flight, each tile's
	LightGridPageSize = Limits::MaxScreenTiles * sizeof(uint32) * 4;
	LightIndexListPageSize = Limits::MaxScreenTiles * Limits::MaxLightsPerTile * sizeof(uint32);

	LightGridBuffer.Create(eGpuBufferType::StorageWithOffset, LightGridPageSize * FramesInFlight, RX_MEMORY_GPU_ONLY);
	LightIndexListBuffer.Create(eGpuBufferType::StorageWithOffset, LightIndexListPageSize * FramesInFlight,
								RX_MEMORY_GPU_ONLY);

	// Decals are written from the CPU every frame, so they get a page per frame in flight like the lights
	DecalPageSize = Limits::MaxVisibleDecals * sizeof(DecalGpuData);
	DecalMaskPageSize = Limits::MaxScreenTiles * Limits::DecalMaskWords * sizeof(uint32);

	DecalBuffer.Create(eGpuBufferType::StorageWithOffset, DecalPageSize * FramesInFlight, RX_MEMORY_CPU_ONLY,
					   eGpuBufferFlags::PersistentMapped);
	DecalMaskBuffer.Create(eGpuBufferType::StorageWithOffset, DecalMaskPageSize * FramesInFlight, RX_MEMORY_GPU_ONLY);

	// Light probes. These only change when probes are placed, baked or loaded, and ProbeManager waits for the GPU to be
	// idle before writing them, so unlike the buffers above they have a single page shared by every frame in flight.
	ProbePageSize = Limits::MaxIrradianceProbes * sizeof(ProbeSHData);
	ProbeBuffer.Create(eGpuBufferType::StorageWithOffset, ProbePageSize, RX_MEMORY_AUTO_PREFER_DEVICE,
					   eGpuBufferFlags::PersistentMapped);

	ProbeVolumePageSize = Limits::MaxProbeVolumes * sizeof(ProbeVolumeData);
	ProbeVolumeBuffer.Create(eGpuBufferType::StorageWithOffset, ProbeVolumePageSize, RX_MEMORY_AUTO_PREFER_DEVICE,
							 eGpuBufferFlags::PersistentMapped);

	ProbeGridPageSize = Limits::MaxProbeGridPoints * sizeof(uint16);
	ProbeGridBuffer.Create(eGpuBufferType::StorageWithOffset, ProbeGridPageSize, RX_MEMORY_AUTO_PREFER_DEVICE,
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


	ReflectionProbePageSize = Limits::MaxReflectionProbes * sizeof(ReflectionProbeData);
	ReflectionProbeBuffer.Create(eGpuBufferType::StorageWithOffset, ReflectionProbePageSize,
								 RX_MEMORY_AUTO_PREFER_DEVICE, eGpuBufferFlags::PersistentMapped);

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
	const uint32 family = GetDevice()->mQueueFamilies.GetTransferFamily();

	pUploadContext = rx_upload_context_new(GetDevice()->GetRustDevice(), GpuAllocator, mpFrameLoop, family);

	if (pUploadContext == nullptr) {
		ModulePanic("Could not create the upload context");
	}

	UploadCmd.WrapExternal(static_cast<VkCommandBuffer>(rx_upload_cmd(pUploadContext)), family);
}

void GraphicsBackend::DestroyUploadContext()
{
	rx_upload_context_free(pUploadContext);
	pUploadContext = nullptr;
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
	}

	// There is one submission semaphore per swapchain image, not frame in flight.
	int32 status = 0;

	mpFrameLoop = rx_frame_loop_new(device->GetRustDevice(), FramesInFlight, Swapchain.OutputImages.Size, &status);

	if (mpFrameLoop == nullptr) {
		PanicVulkan("GraphicsBackend", "Could not create the frame synchronisation", static_cast<VkResult>(status));
	}

	Profiler.Create(device, graphics_family);
}

void GraphicsBackend::DestroyFrames()
{
	rx_gpu_queue_wait_idle(GetDevice()->GetRustDevice(), RX_QUEUE_GRAPHICS);

	// Nothing that uses the query pools is in flight now
	Profiler.Destroy();

	for (auto& frame : Frames) {
		// frame.DescriptorSet.Destroy();

		frame.CmdBuffer.Destroy();
		frame.CmdPool.Destroy();
	}

	Frames.Free();

	rx_frame_loop_destroy(mpFrameLoop, GetDevice()->GetRustDevice());
	mpFrameLoop = nullptr;
}

void GraphicsBackend::RebuildRenderStages()
{
	TiledForwardRenderer* rd = pRenderer;

	Vec2u size = GetWindow()->GetSize();

	// The forward pass shares the prepass's depth image, so the prepass has to be rebuilt first
	rd->Prepass.Rebuild(size);
	rd->ForwardPass.Rebuild(size);
	rd->SSAOPass.Rebuild(size);
	rd->SSAOBlurPass.Rebuild(size);
	rd->CompPass.Rebuild(size);

	gTextRenderer->Resize();

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

	// Allow portability devices (e.g. MoltenVK) to be shown when querying devices. The flag is only
	// valid alongside the portability enumeration extension.
	const bool enumerate_portability = RequiresVulkanPortability(available_extensions);

	if (enumerate_portability) {
		all_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	}

	const RxGpuInstanceConfig instance_config = {
		.app_name = app_name,
		.api_version = app_info.apiVersion,
		.extensions = all_extensions.data(),
		.extension_count = all_extensions.size(),
		.layers = requested_validation_layers.pData,
		.layer_count = requested_validation_layers.Size,
		.enumerate_portability = enumerate_portability,
		.debug_messenger = scEnableValidationLayers,
	};

	const RxLogSink log_sink = { .user = nullptr, .log = RustInterop::Log };

	mpGpuInstance = rx_gpu_instance_create(reinterpret_cast<RxGetInstanceProcAddr>(vkGetInstanceProcAddr),
										   &instance_config, &log_sink);

	if (mpGpuInstance == nullptr) {
		ModulePanic("Could not create vulkan instance!");
	}

	mInstance = reinterpret_cast<VkInstance>(rx_gpu_instance_handle(mpGpuInstance));

	bInitialized = true;
}

void GraphicsBackend::InitGPUAllocator()
{
	GpuAllocator = rx_gpu_allocator_create(mpGpuInstance, GetDevice()->GetRustDevice());

	if (GpuAllocator == nullptr) {
		ModulePanic("Could not create VMA allocator!");
	}
}

void GraphicsBackend::DestroyGPUAllocator()
{
	rx_gpu_allocator_free(GpuAllocator);

	GpuAllocator = nullptr;
}

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
	// Currently, there is nowhere in the engine that requires two separate PC buffers and therefore requires an offset.
	// As well, the small required size of a PC kind of makes this useless. For now, we will ignore this and if needed
	// there will be an updated version of this function.
	// I'm pretty sure when I was using Slang I had one shader that required this, but thats since been cacked..
	static constexpr uint32 scOffset = 0;
	vkCmdPushConstants(cmd.Get(), pipeline.GetLayout(), ShaderUtil::ToUnderlyingType(shader_types), scOffset,
					   data_size, data);
}


void GraphicsBackend::SubmitImmediateUploadCmd(GraphicsBackend::SubmitFunc upload_func)
{
	struct Record
	{
		SubmitFunc& Func;
		uint32 Family;
	} record { upload_func, rx_upload_family(pUploadContext) };

	const VkResult status = static_cast<VkResult>(rx_upload_immediate(
		pUploadContext,
		[](void* user, void* cmd_handle)
		{
			Record& record = *static_cast<Record*>(user);

			CommandBuffer cmd;
			cmd.WrapExternal(static_cast<VkCommandBuffer>(cmd_handle), record.Family);

			record.Func(cmd);
		},
		&record));

	VkTry(status, "Error submitting upload buffer");
}

void GraphicsBackend::SubmitUploadCmd(GraphicsBackend::SubmitFunc upload_func)
{
	upload_func(UploadCmd);
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

	void* commands[] = { cmd.Cmd };

	VkTry(static_cast<VkResult>(rx_gpu_queue_submit(GetDevice()->GetRustDevice(), RX_QUEUE_GRAPHICS, nullptr, 0,
													commands, 1, nullptr, 0, 0)),
		  "Error submitting upload buffer");
	rx_gpu_queue_wait_idle(GetDevice()->GetRustDevice(), RX_QUEUE_GRAPHICS);

	cmd.Reset();
	cmd.Destroy();
}


eFrameResult GraphicsBackend::BeginFrame()
{
	FrameData* frame = GetFrame();

	BeginUploads();

	LightBuffer.Rewind();
	BoneBuffer.Rewind();

	const VkResult wait_status = static_cast<VkResult>(rx_frame_loop_begin(mpFrameLoop, GetDevice()->GetRustDevice()));

	if (wait_status != VK_SUCCESS) {
		PanicVulkan("Fence", "Could not wait for the frame fence", wait_status);
	}

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

	// The tiled light list offsets are applied with the persistent set 0 in World::ExecuteRenderList()
}

void GraphicsBackend::PresentFrame()
{
	SubmitUploads();

	FrameData* frame = GetFrame();

	if (Swapchain.bInitialized != true) {
		ModulePanic("Swapchain not initialized!");
	}

	int32 present_status = 0;

	VkTry(static_cast<VkResult>(rx_frame_loop_submit_and_present(
			  mpFrameLoop, GetDevice()->GetRustDevice(), RxRaw(Swapchain.GetSwapchain()), frame->CmdBuffer.Cmd,
			  RxRaw(rx_upload_transfer_semaphore(pUploadContext)), rx_upload_transfer_count(pUploadContext), &present_status)),
		  "Error submitting draw buffer");

	const VkResult status = static_cast<VkResult>(present_status);

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


static constexpr float32 scDefaultSSAORadius = 0.25f;
static constexpr float32 scDefaultSSAOBias = 0.02f;
static constexpr float32 scDefaultSSAOStrength = 1.5f;
static constexpr float32 scDefaultSSAOPower = 1.2f;
static constexpr float32 scDefaultSSAOFloor = 0.35f;

void GraphicsBackend::RenderEarlyFrameEffects(Camera& camera)
{
	FrameData* frame = GetFrame();

	pRenderer->SSAOPass.Begin(frame->CmdBuffer);

	gPipelineCache->Bind(ePipelineName::SSAO, frame->CmdBuffer);

	const TargetRef target = pRenderer->SSAOPass.GetTarget(eImageFormat::R8_UNorm);
	Assert(target.IsValid());
	const Vec2u target_size = target.GetImage().GetSize();

	SSAOPushConsts consts = {
		.RenderSize = { static_cast<float32>(target_size.X), static_cast<float32>(target_size.Y), },
	};

	memcpy(consts.InvProjection, camera.InvProjectionMatrix.RawData, sizeof(float32) * 16);
	memcpy(consts.Projection, camera.ProjectionMatrix.RawData, sizeof(float32) * 16);
	memcpy(consts.View, camera.ViewMatrix.RawData, sizeof(float32) * 16);

	consts.Radius = gCVars->Get("r_ssao_radius", scDefaultSSAORadius);
	consts.Bias = gCVars->Get("r_ssao_bias", scDefaultSSAOBias);
	consts.Strength = gCVars->Get("r_ssao_strength", scDefaultSSAOStrength);
	consts.Power = gCVars->Get("r_ssao_power", scDefaultSSAOPower);
	consts.Floor = gCVars->Get("r_ssao_floor", scDefaultSSAOFloor);

	SubmitPushConstants(frame->CmdBuffer, gPipelineCache->Request(ePipelineName::SSAO), eShaderType::Pixel, consts);

	vkCmdDraw(frame->CmdBuffer.Get(), 3, 1, 0, 0);

	pRenderer->SSAOPass.End();

	// Bilateral blur pass to smooth SSAO output
	pRenderer->SSAOBlurPass.Begin(frame->CmdBuffer);

	gPipelineCache->Bind(ePipelineName::SSAOBlur, frame->CmdBuffer);

	const TargetRef blur_target = pRenderer->SSAOBlurPass.GetTarget(eImageFormat::R8_UNorm);
	Assert(blur_target.IsValid());
	const Vec2u blur_size = blur_target.GetImage().GetSize();

	SSAOBlurPushConsts blur_consts {};
	rx_forward_ssao_blur_push(blur_size.X, blur_size.Y, &blur_consts);

	SubmitPushConstants(frame->CmdBuffer, gPipelineCache->Request(ePipelineName::SSAOBlur), eShaderType::Pixel,
						blur_consts);

	vkCmdDraw(frame->CmdBuffer.Get(), 3, 1, 0, 0);

	pRenderer->SSAOBlurPass.End();
}

void GraphicsBackend::DoComposition(Camera& render_cam)
{
	FrameData* frame = GetFrame();


	pRenderer->ForwardPass.End();
	MarkGpu(eGpuMarker::Forward);

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
	frame->CmdBuffer.End();

	PresentFrame();

	rx_frame_loop_end_frame(mpFrameLoop);
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
	const VkResult result = static_cast<VkResult>(
		rx_frame_loop_acquire(mpFrameLoop, GetDevice()->GetRustDevice(), RxRaw(Swapchain.GetSwapchain())));

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

	mWindowSurface = mpWindow->CreateSurface(mInstance);
}


void GraphicsBackend::Destroy()
{
	GetDevice()->WaitForIdle();

	rx_upload_forget_frames(pUploadContext);
	DestroyFrames();

	LightBuffer.Destroy();
	BoneBuffer.Destroy();

	LightGridBuffer.Destroy();
	LightIndexListBuffer.Destroy();
	DecalBuffer.Destroy();
	DecalMaskBuffer.Destroy();
	ProbeBuffer.Destroy();
	ProbeVolumeBuffer.Destroy();
	ProbeGridBuffer.Destroy();
	ReflectionProbeBuffer.Destroy();

	gAssetManager->ShutdownDeletionQueue();

	DestroyUploadContext();

	GpuBufferPrintUndestroyed();

	GetDevice()->WaitForIdle();
	Swapchain.Destroy();

	DestroyGPUAllocator();

	if (mWindowSurface) {
		vkDestroySurfaceKHR(mInstance, mWindowSurface, nullptr);
	}

	GetDevice()->Destroy();

	rx_gpu_instance_free(mpGpuInstance);

	mpGpuInstance = nullptr;
	mInstance = nullptr;

	bInitialized = false;
}

FrameData* GraphicsBackend::GetFrame() { return &Frames[GetFrameNumber()]; }

} // namespace fx::renderer
