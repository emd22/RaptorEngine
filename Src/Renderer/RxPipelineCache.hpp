#pragma once

#include <Core/FxItemCache.hpp>
#include <Renderer/Backend/RxPipeline.hpp>

using RxPipelineCacheKey = FxHash32;

struct RxPipelineCacheSection : public FxItemCacheSection_MultiItem<RxPipeline>
{
};

using RxPipelineCacheType = FxItemCache<RxPipelineCacheKey, RxPipeline, RxPipelineCacheSection>;

class RxPipelineCache : public RxPipelineCacheType
{
public:
    using Handle = FxItemCacheHandle<RxPipelineCacheKey, RxPipeline, RxPipelineCacheType>;

public:
    RxPipelineCache() = default;

    void CreatePipeline(const char* name, const FxSlice<RxShaderProgram>& shader_list,
                        const FxSlice<VkAttachmentDescription>& attachments,
                        const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
                        FxVertexInfo* vertex_info, const RxRenderPass& render_pass,
                        const RxPipelineProperties& properties);

    Handle RequestPipeline(const FxHash32 name_hash);
    void ReleasePipeline(const FxHash32 name_hash, RxPipeline* pipeline);

    ~RxPipelineCache() override {}
};
