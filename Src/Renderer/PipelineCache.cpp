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

	mKeys.resize(scNumPipelines);
	mVariantInfos.resize(scNumPipelines);

	for (std::atomic<DynamicPipeline*>& dynamic : mDynamic) {
		dynamic.store(nullptr, std::memory_order_relaxed);
	}

	for (auto& pass_variants : mVariants) {
		for (std::atomic<uint32>& variant : pass_variants) {
			variant.store(PipelineHandle::scInvalidIndex, std::memory_order_relaxed);
		}
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
	const uint32 count = mDynamicCount.load();

	for (uint32 i = 0; i < count; i++) {
		delete mDynamic[i].load();
	}
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
	if (mbHasPending.load(std::memory_order_acquire) && IsMainThread()) {
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
	const uint32 index = mDynamicCount.load(std::memory_order_relaxed);

	if (index >= scMaxDynamicPipelines) {
		LogError(LC_RENDER, "Can not make pipeline '{}', there are already {} pipelines made from descriptions",
				 desc.DebugName, scMaxDynamicPipelines);
		return PipelineHandle {};
	}

	const PipelineHandle handle { scNumPipelines + index };

	DynamicPipeline* dynamic = new DynamicPipeline;
	dynamic->Pipe.Handle = handle;
	dynamic->Pipe.Name = ePipelineName::NumPipelines;
	dynamic->Desc = desc;
	dynamic->DebugName = desc.DebugName;

	mKeys.emplace_back();
	mVariantInfos.emplace_back();

	// The pipeline is set up before it is stored, so whoever finds it finds all of it
	mDynamic[index].store(dynamic, std::memory_order_release);
	mDynamicCount.store(index + 1, std::memory_order_relaxed);

	mPending.push_back(index);
	mbHasPending.store(true, std::memory_order_release);

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
		std::vector<uint32> pending;

		{
			std::lock_guard lock(mMutex);
			pending.swap(mPending);
			mbHasPending.store(false, std::memory_order_release);
		}

		if (pending.empty()) {
			break;
		}

		for (const uint32 index : pending) {
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
	std::lock_guard lock(mMutex);

	AssertLess(handle.Index, mKeys.size());

	KeyEntry& entry = mKeys[handle.Index];

	// The pipeline was rebuilt, so its old key no longer finds it
	if (entry.bRegistered) {
		auto old = mKeyLookup.find(entry.Hash);

		if (old != mKeyLookup.end() && old->second == handle) {
			mKeyLookup.erase(old);
		}
	}

	entry.Key = key;
	entry.Hash = key.GetHash();
	entry.bRegistered = true;

	auto [it, inserted] = mKeyLookup.try_emplace(entry.Hash, handle);

	if (inserted || it->second == handle) {
		return true;
	}

	const KeyEntry& other = mKeys[it->second.Index];

	if (other.Key == key) {
		LogWarning(LC_RENDER, "Pipelines {} and {} were built from identical keys (hash {:#x})", handle.Index,
				   it->second.Index, entry.Hash);
	}
	else {
		LogError(LC_RENDER, "Pipelines {} and {} have different keys with the same hash {:#x}", handle.Index,
				 it->second.Index, entry.Hash);
	}

	return false;
}

PipelineHandle PipelineCache::Find(const PipelineKey& key) const
{
	std::lock_guard lock(mMutex);

	auto it = mKeyLookup.find(key.GetHash());

	// Whatever the hash matched has to have the whole key, or it was a collision
	if (it == mKeyLookup.end() || !(mKeys[it->second.Index].Key == key)) {
		return PipelineHandle {};
	}

	return it->second;
}

std::optional<PipelineKey> PipelineCache::GetKey(const PipelineHandle handle) const
{
	std::lock_guard lock(mMutex);

	if (handle.Index >= mKeys.size() || !mKeys[handle.Index].bRegistered) {
		return std::nullopt;
	}

	return mKeys[handle.Index].Key;
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
	const uint32 pass_index = static_cast<uint32>(pass);
	const uint32 feature_index = static_cast<uint32>(features);

	AssertLess(pass_index, scNumPipelinePasses);
	AssertLess(feature_index, scNumFeatureCombinations);
	AssertLess(handle.Index, mVariantInfos.size());

	mVariants[pass_index][feature_index].store(handle.Index, std::memory_order_release);

	std::vector<PipelineHandle>& pass_pipelines = mPassPipelines[pass_index];

	if (std::find(pass_pipelines.begin(), pass_pipelines.end(), handle) == pass_pipelines.end()) {
		pass_pipelines.push_back(handle);
	}

	// The first registration is the one FindVariantInPass() goes by. Any other would lead to the same pipeline in the
	// other pass, as long as the passes are registered consistently.
	VariantInfo& info = mVariantInfos[handle.Index];

	if (info.Pass == ePipelinePass::Count) {
		info = VariantInfo { .Pass = pass, .Features = features };
	}
}

void PipelineCache::RegisterVariant(const ePipelinePass pass, const ePipelineFeatures features,
									const PipelineHandle handle)
{
	std::lock_guard lock(mMutex);
	RegisterVariantLocked(pass, features, handle);
}

PipelineHandle PipelineCache::FindVariant(const ePipelinePass pass, const ePipelineFeatures features) const
{
	const uint32 pass_index = static_cast<uint32>(pass);
	const uint32 feature_index = static_cast<uint32>(features);

	if (pass_index >= scNumPipelinePasses || feature_index >= scNumFeatureCombinations) {
		return PipelineHandle {};
	}

	return PipelineHandle { mVariants[pass_index][feature_index].load(std::memory_order_acquire) };
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
	ePipelineFeatures features;

	{
		std::lock_guard lock(mMutex);

		if (handle.Index >= mVariantInfos.size() || mVariantInfos[handle.Index].Pass == ePipelinePass::Count) {
			return PipelineHandle {};
		}

		features = mVariantInfos[handle.Index].Features;
	}

	return GetOrCreateVariant(pass, features);
}

PipelineHandle PipelineCache::FindVariantInPass(const PipelineHandle handle, const ePipelinePass pass) const
{
	ePipelineFeatures features;

	{
		std::lock_guard lock(mMutex);

		if (handle.Index >= mVariantInfos.size() || mVariantInfos[handle.Index].Pass == ePipelinePass::Count) {
			return PipelineHandle {};
		}

		features = mVariantInfos[handle.Index].Features;
	}

	return FindVariant(pass, features);
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
