#pragma once

#include "Backend/Pipeline.hpp"
#include "PipelineNames.hpp"


namespace fx::renderer {

class PipelineCache
{
public:
	PipelineCache();

	Pipeline& Request(const ePipelineName name);
	ePipelineName GetName(const Pipeline* pipeline) const;
	void Bind(const ePipelineName name, const CommandBuffer& cmd);

private:
	SizedArray<Pipeline> mCache;
};


} // namespace fx::renderer
