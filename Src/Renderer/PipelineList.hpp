#pragma once

#include "Backend/Pipeline.hpp"

namespace fx::renderer {


using PipelineId = uint32;

class PipelineList
{
public:
    PipelineList() {}

    void Create(uint32 max_pipelines) { Pipelines.InitCapacity(max_pipelines); }

    ~PipelineList() = default;

    FX_FORCE_INLINE PipelineId Insert(Pipeline&& pipeline)
    {
        Pipelines.Insert(std::move(pipeline));
        // Return the last inserted pipeline's index
        return static_cast<PipelineId>(Pipelines.Size - 1);
    }

    FX_FORCE_INLINE Pipeline* Get(PipelineId id)
    {
        if (id > Pipelines.Size) {
            LogError("Pipeline id out of range!");
            return nullptr;
        }

        return &Pipelines[id];
    }

public:
    SizedArray<Pipeline> Pipelines;
};

} // namespace fx::renderer
