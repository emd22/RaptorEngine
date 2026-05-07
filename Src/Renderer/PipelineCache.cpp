#include "PipelineCache.hpp"

namespace fx::renderer {
PipelineCache::PipelineCache() { mCache.InitSize(scNumPipelines); }

Pipeline* PipelineCache::Request(const ePipelineName id)
{
    Pipeline* pipeline = &mCache[static_cast<uint32>(id)];
    return pipeline;
}

void PipelineCache::Bind(const ePipelineName name, const CommandBuffer& cmd)
{
    mCache[static_cast<uint32>(name)].Bind(cmd);
}

} // namespace fx::renderer
