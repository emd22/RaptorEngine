#include "PipelineCache.hpp"

namespace fx::renderer {
PipelineCache::PipelineCache() { mCache.InitSize(scNumPipelines); }

Pipeline& PipelineCache::Request(const ePipelineName id) { return mCache[static_cast<uint32>(id)]; }

void PipelineCache::Bind(const ePipelineName name, const CommandBuffer& cmd) { Request(name).Bind(cmd); }

} // namespace fx::renderer
