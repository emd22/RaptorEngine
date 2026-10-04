#include "PipelineCache.hpp"

#include "PSOBuild.hpp"

#include <Core/Log.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Globals.hpp>
#include <algorithm>

namespace fx::renderer {

static constexpr uint32 scMaxDescriptorSets = 8;
static constexpr uint32 scMaxBuffersPerDS = 6;

PipelineCache::PipelineCache()
{
	// The cache is made on the thread that builds pipelines
	mMainThread = std::this_thread::get_id();

	mCache.InitSize(scNumPipelines);

	for (uint32 i = 0; i < scNumPipelines; i++) {
		mCache[i].Handle = PipelineHandle { i };
	}

	mpRegistry = rx_pipeline_registry_create(scNumPipelines, scMaxDynamicPipelines);

	for (std::atomic<DynamicPipeline*>& dynamic : mDynamic) {
		dynamic.store(nullptr, std::memory_order_relaxed);
	}

	mOffsets.InitCapacity(scMaxDescriptorSets);

	for (uint32 i = 0; i < mOffsets.Capacity; i++) {
		mOffsets[i].pData = nullptr;
		mOffsets[i].Size = 0;
		mOffsets[i].Capacity = 0;
		mOffsets[i].bDoNotDestroy = false;
		mOffsets[i].InitCapacity(scMaxBuffersPerDS);
	}
}

PipelineCache::~PipelineCache()
{
	for (std::atomic<DynamicPipeline*>& dynamic : mDynamic) {
		delete dynamic.load();
	}

	rx_pipeline_registry_free(mpRegistry);
}

Pipeline& PipelineCache::Request(const ePipelineName id)
{
	Pipeline& pl = mCache[static_cast<uint32>(id)];
	pl.Name = id;
	return pl;
}

ePipelineName PipelineCache::GetName(const Pipeline* pipeline) const { return pipeline->Name; }

void PipelineCache::Bind(const ePipelineName name, const CommandBuffer& cmd) { Bind(Request(name).Handle, cmd); }

Pipeline& PipelineCache::Get(const PipelineHandle handle)
{
	if (handle.Index < scNumPipelines) {
		return mCache[handle.Index];
	}

	// Pipelines that other threads asked for are built here, before they are drawn with
	if (rx_pipeline_registry_has_pending(mpRegistry) != 0 && IsMainThread()) {
		BuildPending();
	}

	const uint32 dynamic_index = handle.Index - scNumPipelines;
	AssertLess(dynamic_index, scMaxDynamicPipelines);

	DynamicPipeline* dynamic = mDynamic[dynamic_index].load(std::memory_order_acquire);
	AssertMsg(dynamic != nullptr, "There is no pipeline for this handle");

	return dynamic->Pipe;
}

const char* PipelineCache::GetDebugName(const PipelineHandle handle) const
{
	if (handle.Index < scNumPipelines) {
		return PipelineNameUtil::GetName(static_cast<ePipelineName>(handle.Index));
	}

	const uint32 dynamic_index = handle.Index - scNumPipelines;

	if (dynamic_index >= scMaxDynamicPipelines) {
		return "Unknown";
	}

	const DynamicPipeline* dynamic = mDynamic[dynamic_index].load(std::memory_order_acquire);
	return (dynamic != nullptr) ? dynamic->DebugName.c_str() : "Unknown";
}

PipelineHandle PipelineCache::CreateLocked(const PipelineDesc& desc)
{
	const uint32 handle_index = rx_pipeline_registry_allocate(mpRegistry);

	if (handle_index == RX_INVALID_INDEX) {
		LogError(LC_RENDER, "Can not make pipeline '{}', there are already {} pipelines made from descriptions",
				 desc.DebugName, scMaxDynamicPipelines);
		return PipelineHandle {};
	}

	const PipelineHandle handle { handle_index };

	DynamicPipeline* dynamic = new DynamicPipeline;
	dynamic->Pipe.Handle = handle;
	dynamic->Pipe.Name = ePipelineName::NumPipelines;
	dynamic->Desc = desc;
	dynamic->DebugName = desc.DebugName;

	// The pipeline is set up before it is stored, so whoever finds it finds all of it
	mDynamic[handle_index - scNumPipelines].store(dynamic, std::memory_order_release);

	return handle;
}

PipelineHandle PipelineCache::Create(const PipelineDesc& desc)
{
	PipelineHandle handle;

	{
		std::lock_guard lock(mMutex);
		handle = CreateLocked(desc);
	}

	if (handle.IsValid() && IsMainThread()) {
		BuildPending();

		if (!Get(handle).IsBuilt()) {
			return PipelineHandle {};
		}
	}

	return handle;
}

void PipelineCache::BuildPending()
{
	Assert(IsMainThread());

	// Building a pipeline looks pipelines up, which would build the pending ones again
	if (mbBuilding) {
		return;
	}

	mbBuilding = true;

	while (true) {
		uint32 pending[scMaxDynamicPipelines];
		size_t pending_count = 0;

		{
			std::lock_guard lock(mMutex);
			pending_count = rx_pipeline_registry_take_pending(mpRegistry, pending, scMaxDynamicPipelines);
		}

		if (pending_count == 0) {
			break;
		}

		for (size_t pending_index = 0; pending_index < pending_count; pending_index++) {
			const uint32 index = pending[pending_index];
			DynamicPipeline* dynamic = mDynamic[index].load(std::memory_order_acquire);

			gPSOBuild->Build(PipelineHandle { scNumPipelines + index }, dynamic->Desc);

			if (!dynamic->Pipe.IsBuilt()) {
				LogError(LC_RENDER, "Pipeline '{}' could not be built", dynamic->DebugName);
			}

			// The description is done with, and can hold onto a lot
			dynamic->Desc = PipelineDesc {};
		}
	}

	mbBuilding = false;
}

bool PipelineCache::RegisterKey(const PipelineHandle handle, const PipelineKey& key)
{
	uint32 other = 0;
	uint64 hash = 0;

	const int32 result = rx_pipeline_registry_register_key(
		mpRegistry, handle.Index, reinterpret_cast<const RxPipelineKey*>(&key), &other, &hash);

	if (result == RX_KEY_IDENTICAL) {
		LogWarning(LC_RENDER, "Pipelines {} and {} were built from identical keys (hash {:#x})", handle.Index, other,
				   hash);
	}
	else if (result == RX_KEY_COLLISION) {
		LogError(LC_RENDER, "Pipelines {} and {} have different keys with the same hash {:#x}", handle.Index, other,
				 hash);
	}

	return result == RX_KEY_REGISTERED;
}

PipelineHandle PipelineCache::Find(const PipelineKey& key) const
{
	return PipelineHandle { rx_pipeline_registry_find(mpRegistry, reinterpret_cast<const RxPipelineKey*>(&key)) };
}

std::optional<PipelineKey> PipelineCache::GetKey(const PipelineHandle handle) const
{
	PipelineKey key;

	if (rx_pipeline_registry_get_key(mpRegistry, handle.Index, reinterpret_cast<RxPipelineKey*>(&key)) == 0) {
		return std::nullopt;
	}

	return key;
}

void PipelineCache::Bind(const PipelineHandle handle, const CommandBuffer& cmd)
{
	Pipeline& pl = Get(handle);

	// It could not be built, which was reported when it was tried
	if (!pl.IsBuilt()) {
		return;
	}

	pl.Bind(cmd);

	for (uint32 i = 0; i < pl.DescriptorIDs.Size; i++) {
		Pipeline::DescriptorRef& desc_ref = pl.DescriptorIDs[i];
		Assert(desc_ref.pSet != nullptr);

		desc_ref.pSet->Bind(desc_ref.SetIndex, cmd, pl, Slice<uint32>(mOffsets[desc_ref.SetIndex]));
	}

	Reset();
}

void PipelineCache::RegisterVariantLocked(const ePipelinePass pass, const ePipelineFeatures features,
										  const PipelineHandle handle)
{
	AssertLess(static_cast<uint32>(pass), scNumPipelinePasses);
	AssertLess(static_cast<uint32>(features), scNumFeatureCombinations);

	rx_pipeline_registry_register_variant(mpRegistry, static_cast<uint32>(pass), static_cast<uint32>(features),
										  handle.Index);
}

void PipelineCache::RegisterVariant(const ePipelinePass pass, const ePipelineFeatures features,
									const PipelineHandle handle)
{
	std::lock_guard lock(mMutex);
	RegisterVariantLocked(pass, features, handle);
}

PipelineHandle PipelineCache::FindVariant(const ePipelinePass pass, const ePipelineFeatures features) const
{
	return PipelineHandle { rx_pipeline_registry_find_variant(mpRegistry, static_cast<uint32>(pass),
															  static_cast<uint32>(features)) };
}

void PipelineCache::RegisterPassTemplate(const ePipelinePass pass, PassTemplate pass_template)
{
	AssertLess(static_cast<uint32>(pass), scNumPipelinePasses);

	std::lock_guard lock(mMutex);
	mPassTemplates[static_cast<uint32>(pass)] = std::move(pass_template);
}

PipelineHandle PipelineCache::GetOrCreateVariant(const ePipelinePass pass, const ePipelineFeatures features)
{
	PipelineHandle handle = FindVariant(pass, features);

	if (handle.IsValid()) {
		return handle;
	}

	{
		std::lock_guard lock(mMutex);

		// Another thread may have made it while this one waited
		handle = FindVariant(pass, features);

		if (handle.IsValid()) {
			return handle;
		}

		const PassTemplate& pass_template = mPassTemplates[static_cast<uint32>(pass)];

		PipelineDesc desc;

		if (!pass_template || !pass_template(features, desc)) {
			return PipelineHandle {};
		}

		// The pipeline that draws these features may be one that already exists
		handle = FindVariant(pass, desc.Features);

		if (!handle.IsValid()) {
			handle = CreateLocked(desc);

			if (!handle.IsValid()) {
				return PipelineHandle {};
			}

			RegisterVariantLocked(pass, desc.Features, handle);
		}

		if (features != desc.Features) {
			RegisterVariantLocked(pass, features, handle);
		}
	}

	// On the main thread, the caller gets a pipeline it can draw with
	if (IsMainThread()) {
		BuildPending();
	}

	return handle;
}

PipelineHandle PipelineCache::GetOrCreateVariantInPass(const PipelineHandle handle, const ePipelinePass pass)
{
	uint32 handle_pass = 0;
	uint32 features = 0;

	if (rx_pipeline_registry_variant_info(mpRegistry, handle.Index, &handle_pass, &features) == 0) {
		return PipelineHandle {};
	}

	return GetOrCreateVariant(pass, static_cast<ePipelineFeatures>(features));
}

PipelineHandle PipelineCache::FindVariantInPass(const PipelineHandle handle, const ePipelinePass pass) const
{
	uint32 handle_pass = 0;
	uint32 features = 0;

	if (rx_pipeline_registry_variant_info(mpRegistry, handle.Index, &handle_pass, &features) == 0) {
		return PipelineHandle {};
	}

	return FindVariant(pass, static_cast<ePipelineFeatures>(features));
}

void PipelineCache::AddBufferOffset(uint32 set_index, uint32 offset) { mOffsets[set_index].Insert(offset); }

void PipelineCache::Reset()
{
	for (uint32 i = 0; i < mOffsets.Capacity; i++) {
		mOffsets[i].Size = 0;
	}

	mOffsets.Size = 0;
}


} // namespace fx::renderer
