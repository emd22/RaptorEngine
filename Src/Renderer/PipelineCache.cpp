#include "PipelineCache.hpp"

namespace fx::renderer {
PipelineCache::PipelineCache() { mCache.InitSize(scNumPipelines); }

Pipeline* PipelineCache::Request(const ePipelineName id)
{
    Pipeline* pipeline = &mCache[static_cast<uint32>(id)];
    return pipeline;
}

} // namespace fx::renderer
