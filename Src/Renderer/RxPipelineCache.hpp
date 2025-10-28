#pragma once

#include <Core/FxItemCache.hpp>
#include <Renderer/Backend/RxPipeline.hpp>

using RxPipelineCacheKey = FxHash;

struct RxPipelineCacheSection : public FxItemCacheSection_MultiItem<RxGraphicsPipeline>
{
};

using RxPipelineCacheType = FxItemCache<RxPipelineCacheKey, RxGraphicsPipeline, RxPipelineCacheSection>;

class RxPipelineCache : public RxPipelineCacheType
{
public:
    using Handle = FxItemCacheHandle<RxPipelineCacheKey, RxGraphicsPipeline, RxPipelineCacheType>;

public:
    RxPipelineCache() = default;

    void CreatePipeline(const char* name, const FxSlice<RxShader>& shader_list,
                        const FxSlice<VkAttachmentDescription>& attachments,
                        const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
                        FxVertexInfo* vertex_info, const RxRenderPass& render_pass,
                        const RxGraphicsPipelineProperties& properties);

    Handle RequestPipeline(const FxHash name_hash);
    void ReleasePipeline(const FxHash name_hash, RxGraphicsPipeline* pipeline);

    ~RxPipelineCache() override {}
};
