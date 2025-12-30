#include "RxShadowDirectional.hpp"

#include <FxEngine.hpp>
#include <Renderer/Backend/RxDsLayoutBuilder.hpp>
#include <Renderer/RxPipelineBuilder.hpp>
#include <Renderer/RxRenderBackend.hpp>

struct alignas(16) PushConstants
{
    float32 CameraMatrix[16];
    uint32 ObjectId = 0;
};

RxShadowDirectional::RxShadowDirectional(const FxVec2u& size)
{
    RxAttachmentList attachment_list;
    attachment_list.Add({ .Format = VK_FORMAT_D32_SFLOAT });
    mRenderPass.Create(attachment_list);

    mAttachment.Create(RxImageType::eImage, size, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT);
    mFramebuffer.Create({ mAttachment.View }, mRenderPass, size);

    ShadowCamera.Update();

    mCommandBuffer.Create(&gRenderer->GetFrame()->CommandPool);

    FxStackArray<RxDescriptorSet, 1> desc_sets = {

    };

    FxStackArray<RxPushConstants, 1> push_consts = { RxPushConstants { .Size = sizeof(PushConstants),
                                                                       .StageFlags = VK_SHADER_STAGE_VERTEX_BIT } };
    VkPipelineLayout pipeline_layout = RxPipeline::CreateLayout(FxSlice(push_consts), FxSlice(desc_sets));

    RxShader shader_shadow("Shadow");
    FxRef<RxShaderProgram> vertex_shader = shader_shadow.GetProgram(RxShaderType::eVertex, {});
    FxRef<RxShaderProgram> fragment_shader = shader_shadow.GetProgram(RxShaderType::eFragment, {});

    FxVertexInfo vertex_info = FxMakeVertexInfo();

    RxPipelineBuilder builder {};
    builder.SetLayout(pipeline_layout)
        .SetName("Shadow Pipeline")
        .AddBlendAttachment({ .Enabled = false })
        .SetAttachments(&attachment_list)
        .SetShaders(vertex_shader, fragment_shader)
        .SetRenderPass(&mRenderPass)
        .SetVertexInfo(&vertex_info)
        .SetCullMode(VK_CULL_MODE_BACK_BIT)
        .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

    builder.Build(mPipeline);
}

void RxShadowDirectional::Begin()
{
    VkClearValue clear_values[] = {
        // Output color
        VkClearValue { .depthStencil = { 0.0f, 0 } },
    };

    mRenderPass.Begin(&mCommandBuffer, mFramebuffer.Framebuffer,
                      FxMakeSlice(clear_values, FxSizeofArray(clear_values)));
    mPipeline.Bind(mCommandBuffer);
}
