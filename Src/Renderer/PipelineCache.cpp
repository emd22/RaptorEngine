#include "PipelineCache.hpp"

namespace fx::renderer {
PipelineCache::PipelineCache() { mCache.InitSize(scNumPipelines); }

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

void PipelineCache::Bind(const ePipelineName name, const CommandBuffer& cmd) { Request(name).Bind(cmd); }

} // namespace fx::renderer
