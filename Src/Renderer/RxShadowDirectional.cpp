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
    RxAttachmentList attachment_list;
    attachment_list.Add({ .Format = VK_FORMAT_D32_SFLOAT_S8_UINT });
    mRenderPass.Create(attachment_list, size);

    for (int i = 0; i < RendererFramesInFlight; i++) {
        mAttachments[i].Create(RxImageType::eImage, size, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_TILING_OPTIMAL,
                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_IMAGE_ASPECT_DEPTH_BIT);
        mFramebuffers[i].Create({ mAttachments[i].View }, mRenderPass, size);
    }
    ShadowCamera.Update();

    mDescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5);
    mDescriptorPool.Create(gRenderer->GetDevice());

    // mCommandBuffer.Create(&gRenderer->GetFrame()->CommandPool);

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

    for (int i = 0; i < RendererFramesInFlight; i++) {
        const RxDeferredLightingPass& lighting_pass = gRenderer->pDeferredRenderer->LightingPasses[i];
        UpdateDescriptorSet(i, lighting_pass);
    }
}

void RxShadowDirectional::Begin()
{
    VkClearValue clear_values[] = {
        // Output color
        VkClearValue { .depthStencil = { 0.0f, 0 } },
    };

    mCommandBuffer = &gRenderer->GetFrame()->ShadowCommandBuffer;

    mCommandBuffer->Reset();
    mCommandBuffer->Record();

    mRenderPass.Begin(mCommandBuffer, mFramebuffers[gRenderer->GetFrameNumber()].Framebuffer,
                      FxMakeSlice(clear_values, FxSizeofArray(clear_values)));


    VkDescriptorSet desc_sets[] = { gObjectManager->mObjectBufferDS.Set };

    RxDescriptorSet::BindMultiple(*mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline,
                                  FxSlice(desc_sets, FxSizeofArray(desc_sets)));
    mPipeline.Bind(*mCommandBuffer);
}

void RxShadowDirectional::End()
{
    mRenderPass.End();

    mCommandBuffer->End();


    RxFrameData* frame = gRenderer->GetFrame();

    const VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT };

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->ImageAvailable.Semaphore,
        .pWaitDstStageMask = wait_stages,
        // command buffers
        .commandBufferCount = 1,
        .pCommandBuffers = &mCommandBuffer->CommandBuffer,
        // signal semaphores
        .signalSemaphoreCount = 1,
        // .pSignalSemaphores = &frame->RenderFinished.Semaphore
        .pSignalSemaphores = &frame->ShadowsSem.Semaphore,
    };

    VkTry(vkQueueSubmit(gRenderer->GetDevice()->GraphicsQueue, 1, &submit_info, VK_NULL_HANDLE),
          "Error submitting draw buffer");
}

void RxShadowDirectional::UpdateDescriptorSet(int index, const RxDeferredLightingPass& lighting_pass)
{
    FxStackArray<VkDescriptorImageInfo, 4> write_image_infos;
    FxStackArray<VkWriteDescriptorSet, 5> write_infos;

    // Shadow Depth image descriptor
    {
        const int binding_index = 4;

        const VkDescriptorImageInfo depth_image_info {
            .sampler = gRenderer->Swapchain.ShadowDepthSampler.Sampler,
            .imageView = mAttachments[index].View,
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
