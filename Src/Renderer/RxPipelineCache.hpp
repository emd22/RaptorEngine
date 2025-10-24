#pragma once

#include <Core/FxItemCache.hpp>
#include <Renderer/Backend/RxPipeline.hpp>

struct RxPipelineCacheSection : public FxItemCacheSection<RxGraphicsPipeline>
{
};

class RxPipelineCache : public FxItemCache<RxGraphicsPipeline, RxPipelineCacheSection>
{
public:
    using Handle = FxItemCacheHandle<RxGraphicsPipeline, RxPipelineCacheSection>;

public:
    RxPipelineCache() = default;

    void CreatePipeline(const char* name, const FxSlice<RxShader>& shader_list,
                        const FxSlice<VkAttachmentDescription>& attachments,
                        const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
                        FxVertexInfo* vertex_info, const RxRenderPass& render_pass,
                        const RxGraphicsPipelineProperties& properties);

    Handle RequestPipeline(const FxHash name_hash);
    void ReleasePipeline(const FxHash name_hash, RxGraphicsPipeline* pipeline);

    ~RxPipelineCache() override { Destroy(); }
};
