#include "RxPipelineCache.hpp"

void RxPipelineCache::CreatePipeline(const char* name, const FxSlice<RxShaderProgram>& shader_list,
                                     const FxSlice<VkAttachmentDescription>& attachments,
                                     const FxSlice<VkPipelineColorBlendAttachmentState>& color_blend_attachments,
                                     FxVertexInfo* vertex_info, const RxRenderPass& render_pass,
                                     const RxGraphicsPipelineProperties& properties)
{
    const FxHash32 pipeline_hash = FxHashStr32(name);

    if (IsItemInCache(pipeline_hash)) {
        FxLogWarning("Pipeline '{}' has already been registered in the pipeline cache!");
        return;
    }

    RxGraphicsPipeline* pipeline = RequestGenericItem(pipeline_hash);

    pipeline->Create(name, shader_list, attachments, color_blend_attachments, vertex_info, render_pass, properties);

    RxPipelineCacheSection& cache_section = mCache[pipeline_hash];

    for (auto& item : cache_section.Items) {
        item = (*pipeline);
    }

    ReleaseGenericItem(pipeline_hash, pipeline);
}

RxPipelineCache::Handle RxPipelineCache::RequestPipeline(const FxHash32 name_hash)
{
    RxGraphicsPipeline* pipeline = RequestGenericItem(name_hash);
    FxDebugAssert(pipeline->Pipeline != nullptr);

    return RxPipelineCache::Handle(this, pipeline, name_hash);
}

void RxPipelineCache::ReleasePipeline(const FxHash32 name_hash, RxGraphicsPipeline* pipeline)
{
    ReleaseGenericItem(name_hash, pipeline);
}
