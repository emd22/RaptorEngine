#pragma once

#include "PSOBuild.hpp"

namespace fx::renderer {

/**
 * @brief What a pass's template fills in to describe a pipeline, see PipelineCache::RegisterPassTemplate()
 */
using PipelineDesc = PSOBuild;

} // namespace fx::renderer
