#include "PipelineCache.hpp"

#include "PSOBuild.hpp"

#include <Core/Log.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/ShaderLibrary.hpp>
#include <Util/RustInterop.hpp>

namespace fx::renderer {

static constexpr uint32 scMaxDynamicPipelines = 256;

PipelineCache::PipelineCache()
{
	mpCache = rx_pipeline_cache_new(scNumPipelines, scMaxDynamicPipelines);

	mLog = RxLogSink { .user = nullptr, .log = RustInterop::Log };
}

PipelineCache::~PipelineCache()
{
	GraphicsBackend* graphics = gGraphics;

	const bool can_free = (graphics != nullptr) && (graphics->GetDevice()->GetRustDevice() != nullptr);

	rx_pipeline_cache_free(mpCache, can_free ? graphics->GetDevice()->GetRustDevice() : nullptr);
	mpCache = nullptr;
}

RxPipelineContext PipelineCache::MakeContext() const
{
	return RxPipelineContext {
		.device = gGraphics->GetDevice()->GetRustDevice(),
		.descriptor_cache = gDescriptorCache->GetRust(),
		.ds_layouts = gDsLayoutCache->GetRustCache(),
		.library = gShaderLibrary->GetRust(),
		.log = &mLog,
	};
}

void PipelineCache::CheckStatus(int32 status, int32 vk_result)
{
	switch (status) {
	case RX_PIPELINE_OK:
		break;
	case RX_PIPELINE_DESCRIPTOR_MISMATCH:
		Panic("PSOBuild", "Descriptor mismatch");
		break;
	case RX_PIPELINE_VULKAN_ERROR:
		PanicVulkan("Pipeline", "Could not create pipeline", static_cast<VkResult>(vk_result));
		break;
	case RX_PIPELINE_BLEND_TARGET:
		Panic("PSOBuild", "A blend attachment targets an attachment that does not exist");
		break;
	default:
		Panic("RecompileShader", "Error compiling shaders");
	}
}

Pipeline& PipelineCache::Request(const ePipelineName id) { return Get(GetHandle(id)); }

void PipelineCache::Bind(const ePipelineName name, const CommandBuffer& cmd) { Bind(GetHandle(name), cmd); }

Pipeline& PipelineCache::Get(const PipelineHandle handle)
{
	// Pipelines that other threads asked for are built here, before they are drawn with
	if (handle.Index >= scNumPipelines && rx_pipeline_cache_has_pending(mpCache) != 0 &&
		rx_pipeline_cache_is_main_thread(mpCache) != 0) {
		BuildPending();
	}

	RxPipelineSlot* slot = rx_pipeline_cache_slot(mpCache, handle.Index);
	AssertMsg(slot != nullptr, "There is no pipeline for this handle");

	return *static_cast<Pipeline*>(slot);
}

const char* PipelineCache::GetDebugName(const PipelineHandle handle) const
{
	const RxPipelineSlot* slot = rx_pipeline_cache_slot(mpCache, handle.Index);

	return (slot != nullptr) ? slot->debug_name : "Unknown";
}

void PipelineCache::BuildPending()
{
	int32 vk_result = 0;

	const RxPipelineContext context = MakeContext();

	CheckStatus(rx_pipeline_cache_build_pending(mpCache, &context, &vk_result), vk_result);
}

void PipelineCache::BuildStatic(const PipelineHandle handle, RxPipelineDesc* desc)
{
	int32 vk_result = 0;

	const RxPipelineContext context = MakeContext();

	CheckStatus(rx_pipeline_cache_build_static(mpCache, &context, handle.Index, desc, &vk_result), vk_result);
}

void PipelineCache::Bind(const PipelineHandle handle, const CommandBuffer& cmd)
{
	Pipeline& pl = Get(handle);

	// It could not be built, which was reported when it was tried
	if (!pl.IsBuilt()) {
		return;
	}

	pl.Bind(cmd);

	const bool matched = rx_pipeline_cache_bind_sets(mpCache, gGraphics->GetDevice()->GetRustDevice(), &pl, cmd.Cmd,
													 pl.GetBindPoint()) != 0;

	AssertMsg(matched, "The buffer offsets do not match the buffers in the pipeline's descriptor sets");
}

PipelineHandle PipelineCache::FindVariant(const ePipelinePass pass, const ePipelineFeatures features) const
{
	return PipelineHandle { rx_pipeline_cache_find_variant(mpCache, static_cast<uint32>(pass),
														   static_cast<uint32>(features)) };
}

PipelineHandle PipelineCache::FindVariantInPass(const PipelineHandle handle, const ePipelinePass pass) const
{
	return PipelineHandle { rx_pipeline_cache_find_variant_in_pass(mpCache, handle.Index, static_cast<uint32>(pass)) };
}

void PipelineCache::RegisterPassTemplate(const ePipelinePass pass, PassTemplate pass_template)
{
	AssertLess(static_cast<uint32>(pass), scNumPipelinePasses);

	rx_pipeline_cache_register_template(
		mpCache, static_cast<uint32>(pass),
		[](void* user, uint32, uint32 features, RxPipelineDesc* desc) -> uint8
		{
			PSOBuild pso(desc);
			return (*static_cast<PassTemplate*>(user))(static_cast<ePipelineFeatures>(features), pso) ? 1 : 0;
		},
		new PassTemplate(std::move(pass_template)), [](void* user) { delete static_cast<PassTemplate*>(user); });
}

PipelineHandle PipelineCache::GetOrCreateVariant(const ePipelinePass pass, const ePipelineFeatures features)
{
	uint32 handle = RX_INVALID_INDEX;
	int32 vk_result = 0;

	const RxPipelineContext context = MakeContext();

	CheckStatus(rx_pipeline_cache_get_or_create_variant(mpCache, &context, static_cast<uint32>(pass),
														static_cast<uint32>(features), &handle, &vk_result),
				vk_result);

	return PipelineHandle { handle };
}

PipelineHandle PipelineCache::GetOrCreateVariantInPass(const PipelineHandle handle, const ePipelinePass pass)
{
	uint32 variant = RX_INVALID_INDEX;
	int32 vk_result = 0;

	const RxPipelineContext context = MakeContext();

	CheckStatus(rx_pipeline_cache_get_or_create_variant_in_pass(mpCache, &context, handle.Index,
																static_cast<uint32>(pass), &variant, &vk_result),
				vk_result);

	return PipelineHandle { variant };
}

void PipelineCache::AddBufferOffset(uint32 set_index, uint32 offset)
{
	rx_pipeline_cache_add_buffer_offset(mpCache, set_index, offset);
}

} // namespace fx::renderer
