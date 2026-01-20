#include "RxShadowDirectional.hpp"

#include <FxEngine.hpp>
#include <FxObjectManager.hpp>
#include <Renderer/Backend/RxDsLayoutBuilder.hpp>
#include <Renderer/Backend/RxUtil.hpp>
#include <Renderer/RxDeferred.hpp>
#include <Renderer/RxPipelineBuilder.hpp>
#include <Renderer/RxRenderBackend.hpp>

FX_SET_MODULE_NAME("RxShadowDirectional")


RxShadowDirectional::RxShadowDirectional(const FxVec2u& size)
{
    RenderStage.Create(size);

    RenderStage.AddTarget(RxImageFormat::eD32_Float, size,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          RxImageAspectFlag::eDepth);

    RenderStage.BuildRenderStage();

    ShadowCamera.Update();

    mDescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5);
    mDescriptorPool.Create(gRenderer->GetDevice());

    RxDsLayoutBuilder ds_layout_builder {};
    mDsLayout = ds_layout_builder.Build();
    mDescriptorSet.Create(mDescriptorPool, mDsLayout);

    FxStackArray<VkDescriptorSetLayout, 2> desc_sets = {
        gObjectManager->DsLayoutObjectBuffer,
    };

    FxStackArray<RxPushConstants, 1> push_consts = {
        RxPushConstants { .Size = sizeof(RxShadowPushConstants), .StageFlags = VK_SHADER_STAGE_VERTEX_BIT },
    };

    VkPipelineLayout pipeline_layout = RxPipeline::CreateLayout(FxSlice(push_consts), FxSlice(desc_sets));

    RxShader shader_shadow("Shadows");
    FxRef<RxShaderProgram> vertex_shader = shader_shadow.GetProgram(RxShaderType::eVertex, {});
    FxRef<RxShaderProgram> fragment_shader = shader_shadow.GetProgram(RxShaderType::eFragment, {});

    FxVertexInfo vertex_info = FxMakeVertexInfo();

    RxPipelineProperties pipeline_properties {
        .ViewportSize = size,
        .DepthCompareOp = VK_COMPARE_OP_GREATER,
    };

    RxPipelineBuilder builder {};
    builder.SetLayout(pipeline_layout)
        .SetName("Shadow Pipeline")
        .AddBlendAttachment({ .Enabled = false })
        .SetProperties(pipeline_properties)
        .SetAttachments(&RenderStage.GetTargets())
        .SetShaders(vertex_shader, fragment_shader)
        .SetRenderPass(&RenderStage.GetRenderPass())
        .SetVertexInfo(&vertex_info)
        .SetCullMode(VK_CULL_MODE_BACK_BIT)
        .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

    builder.Build(mPipeline);

    for (int i = 0; i < RxFramesInFlight; i++) {
        const RxDeferredLightingPass& lighting_pass = gRenderer->pDeferredRenderer->LightingPasses[i];
        UpdateDescriptorSet(i, lighting_pass);
    }
}

void RxShadowDirectional::Begin()
{
    RxCommandBuffer& cmd = gRenderer->GetFrame()->CommandBuffer;

    RenderStage.Begin(cmd, mPipeline);

    VkDescriptorSet desc_sets[] = { gObjectManager->mObjectBufferDS.Set };
    RxDescriptorSet::BindMultiple(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline,
                                  FxSlice(desc_sets, FxSizeofArray(desc_sets)));
}

void RxShadowDirectional::End() { RenderStage.End(); }

void RxShadowDirectional::UpdateDescriptorSet(int index, const RxDeferredLightingPass& lighting_pass)
{
    FxStackArray<VkDescriptorImageInfo, 4> write_image_infos;
    FxStackArray<VkWriteDescriptorSet, 5> write_infos;

    // Shadow Depth image descriptor
    {
        const int binding_index = 4;

        const VkDescriptorImageInfo depth_image_info {
            .sampler = gRenderer->Swapchain.ShadowDepthSampler.Sampler,
            .imageView = RenderStage.GetTarget(RxImageFormat::eD32_Float, 0)->GetImageView(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkWriteDescriptorSet depth_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = lighting_pass.DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = write_image_infos.Insert(depth_image_info),
        };

        write_infos.Insert(depth_write);

        vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, write_infos.Size, write_infos.pData, 0, nullptr);
    }
}
