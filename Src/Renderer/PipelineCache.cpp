#include "PipelineCache.hpp"

#include <Core/Log.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Globals.hpp>

namespace fx::renderer {

static constexpr uint32 scMaxDescriptorSets = 8;
static constexpr uint32 scMaxBuffersPerDS = 6;

PipelineCache::PipelineCache()
{
	mCache.InitSize(scNumPipelines);

	for (uint32 i = 0; i < scNumPipelines; i++) {
		mCache[i].Handle = PipelineHandle { i };
	}

	mKeys.resize(scNumPipelines);

	mOffsets.InitCapacity(scMaxDescriptorSets);

	for (uint32 i = 0; i < mOffsets.Capacity; i++) {
		mOffsets[i].pData = nullptr;
		mOffsets[i].Size = 0;
		mOffsets[i].Capacity = 0;
		mOffsets[i].bDoNotDestroy = false;
		mOffsets[i].InitCapacity(scMaxBuffersPerDS);
	}
}

Pipeline& PipelineCache::Request(const ePipelineName id)
{
	Pipeline& pl = mCache[static_cast<uint32>(id)];
	pl.Name = id;
	return pl;
}

ePipelineName PipelineCache::GetName(const Pipeline* pipeline) const
{
	return pipeline->Name;

	// for (uint32 i = 0; i < mCache.Size; i++) {
	// 	if ((&mCache[i]) == pipeline) {
	// 		return static_cast<ePipelineName>(i);
	// 	}
	// }

	// return ePipelineName::Geometry;
}

void PipelineCache::Bind(const ePipelineName name, const CommandBuffer& cmd) { Bind(Request(name).Handle, cmd); }

Pipeline& PipelineCache::Get(const PipelineHandle handle)
{
	AssertLess(handle.Index, mCache.Size);
	return mCache[handle.Index];
}

bool PipelineCache::RegisterKey(const PipelineHandle handle, const PipelineKey& key)
{
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
	auto it = mKeyLookup.find(key.GetHash());

	// Whatever the hash matched has to have the whole key, or it was a collision
	if (it == mKeyLookup.end() || !(mKeys[it->second.Index].Key == key)) {
		return PipelineHandle {};
	}

	return it->second;
}

const PipelineKey* PipelineCache::GetKey(const PipelineHandle handle) const
{
	if (handle.Index >= mKeys.size() || !mKeys[handle.Index].bRegistered) {
		return nullptr;
	}

	return &mKeys[handle.Index].Key;
}

void PipelineCache::Bind(const PipelineHandle handle, const CommandBuffer& cmd)
{
	Pipeline& pl = Get(handle);

	pl.Bind(cmd);

	for (uint32 i = 0; i < pl.DescriptorIDs.Size; i++) {
		Pipeline::DescriptorRef& desc_ref = pl.DescriptorIDs[i];
		Assert(desc_ref.pSet != nullptr);

		desc_ref.pSet->Bind(desc_ref.SetIndex, cmd, pl, Slice<uint32>(mOffsets[desc_ref.SetIndex]));
	}

	Reset();
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
