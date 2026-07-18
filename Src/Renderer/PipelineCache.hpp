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

	void SetBufferOffset(uint32 bind_index, uint32 set_index, uint32 offset);

private:
	void Reset();

private:
	SizedArray<Pipeline> mCache;
	SizedArray<SizedArray<uint32>> mOffsets;
};


} // namespace fx::renderer
