#include "PipelineCache.hpp"

#include <Core/Types.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Globals.hpp>

namespace fx::renderer {

static constexpr uint32 scMaxDescriptorSets = 8;
static constexpr uint32 scMaxBuffersPerDS = 6;

PipelineCache::PipelineCache()
{
	mCache.InitSize(scNumPipelines);

	mOffsets.InitCapacity(scMaxDescriptorSets);

	for (uint32 i = 0; i < mOffsets.Capacity; i++) {
		mOffsets[i].pData = nullptr;
		mOffsets[i].Size = 0;
		mOffsets[i].Capacity = 0;
		mOffsets[i].bDoNotDestroy = false;
		mOffsets[i].InitCapacity(scMaxBuffersPerDS);
	}
}

Pipeline& PipelineCache::Request(const ePipelineName id) { return mCache[static_cast<uint32>(id)]; }

ePipelineName PipelineCache::GetName(const Pipeline* pipeline) const
{
	for (uint32 i = 0; i < mCache.Size; i++) {
		if ((&mCache[i]) == pipeline) {
			return static_cast<ePipelineName>(i);
		}
	}

	return ePipelineName::Geometry;
}

void PipelineCache::Bind(const ePipelineName name, const CommandBuffer& cmd)
{
	Pipeline& pl = Request(name);

	pl.Bind(cmd);

	// if (pl.bBindAttachedDescriptors) {
	for (uint32 i = 0; i < pl.DescriptorIDs.Size; i++) {
		Pipeline::DescriptorRef& desc_ref = pl.DescriptorIDs[i];
		Assert(desc_ref.ID != HashNull32);

		gDescriptorCache->Request(desc_ref.ID)
			->Bind(desc_ref.SetIndex, cmd, pl, Slice<uint32>(mOffsets[desc_ref.SetIndex]));
	}
	// }

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
