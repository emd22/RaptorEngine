#include "RxPipelineCache.hpp"

void RxPipelineCache::CreatePipeline(const char* name, const FxSlice<RxShader>& shader_list,
                                     const FxSlice<VkAttachmentDescription>& attachments,
                                     const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
                                     FxVertexInfo* vertex_info, const RxRenderPass& render_pass,
                                     const RxGraphicsPipelineProperties& properties)
{
    const FxHash pipeline_hash = FxHashStr(name);

    if (IsItemInCache(pipeline_hash)) {
        FxLogWarning("Pipeline '{}' has already been registered in the pipeline cache!");
        return;
    }

    RxGraphicsPipeline* pipeline = RequestItemFromHash(pipeline_hash);

    pipeline->Create(name, shader_list, attachments, color_blend_attachments, vertex_info, render_pass, properties);

    RxPipelineCacheSection& cache_section = mCache[pipeline_hash];

    for (auto& item : cache_section.Items) {
        item = (*pipeline);
    }

    ReleaseItemFromHash(pipeline_hash, pipeline);
}

RxPipelineCache::Handle RxPipelineCache::RequestPipeline(const FxHash name_hash)
{
    RxGraphicsPipeline* pipeline = RequestItemFromHash(name_hash);
    FxDebugAssert(pipeline->Pipeline != nullptr);

    return RxPipelineCache::Handle(this, pipeline, name_hash);
}

void RxPipelineCache::ReleasePipeline(const FxHash name_hash, RxGraphicsPipeline* pipeline)
{
    ReleaseItemFromHash(name_hash, pipeline);
}
