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

	for (uint32 i = 0; i < pl.DescriptorIDs.Size; i++) {
		Hash32 ds_id = pl.DescriptorIDs[i];
		if (ds_id == HashNull32) {
			continue;
		}

		gDescriptorCache->Request(ds_id)->Bind(cmd, pl, Slice<uint32>(mOffsets[i]));
	}

	Reset();
}

void PipelineCache::SetBufferOffset(uint32 bind_index, uint32 set_index, uint32 offset)
{
	mOffsets[set_index][bind_index] = offset;
}

void PipelineCache::Reset()
{
	for (uint32 i = 0; i < mOffsets.Size; i++) {
		mOffsets[i].Size = 0;
	}

	mOffsets.Size = 0;
}


} // namespace fx::renderer
