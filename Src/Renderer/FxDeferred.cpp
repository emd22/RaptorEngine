#include "FxDeferred.hpp"
#include "Renderer.hpp"

#include "Backend/ShaderList.hpp"
#include "Backend/RvkShader.hpp"

FX_SET_MODULE_NAME("DeferredRenderer")

void FxDeferredRenderer::Create(const Vec2u& extent)
{
    CreateGPassPipeline();

    GPasses.Size = GPasses.Capacity;
    for (int i = 0; i < RendererFramesInFlight; i++) {
        GPasses[i].Create(this, extent);
    }
}

void FxDeferredRenderer::Destroy()
{
    for (int i = 0; i < RendererFramesInFlight; i++) {
        GPasses[i].Destroy();
    }

    GPassPipeline.Destroy();
}

FxDeferredGPass* FxDeferredRenderer::GetCurrentGPass()
{
    return &GPasses[Renderer->GetFrameNumber()];
}

void FxDeferredRenderer::CreateGPassPipeline()
{
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        // Positions
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                            | VK_COLOR_COMPONENT_G_BIT
                            | VK_COLOR_COMPONENT_B_BIT
                            | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        },
        // Color
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                            | VK_COLOR_COMPONENT_G_BIT
                            | VK_COLOR_COMPONENT_B_BIT
                            | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        },
    };

    ShaderList shader_list;

    RvkShader vertex_shader("../shaders/main.vert.spv", RvkShaderType::Vertex);
    RvkShader fragment_shader("../shaders/main.frag.spv", RvkShaderType::Fragment);

    shader_list.Vertex = vertex_shader.ShaderModule;
    shader_list.Fragment = fragment_shader.ShaderModule;

    const int attachment_count = FxSizeofArray(color_blend_attachments);
    GPassPipeline.Create(shader_list, GPassPipeline.CreateGPassLayout(), FxMakeSlice(color_blend_attachments, attachment_count), false);
}


////////////////////////
// G-Pass functions
////////////////////////


void FxDeferredGPass::BuildDescriptorSets()
{
    DescriptorSet.Create(DescriptorPool, mGPassPipeline->MainDescriptorSetLayout);

    VkDescriptorBufferInfo ubo_info{
        .buffer = UniformBuffer.Buffer,
        .offset = 0,
        .range = sizeof(RvkUniformBufferObject)
    };

    VkWriteDescriptorSet ubo_write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .dstSet = DescriptorSet,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .pBufferInfo = &ubo_info,
    };

    const VkWriteDescriptorSet writes[] = { ubo_write };

    vkUpdateDescriptorSets(Renderer->GetDevice()->Device, sizeof(writes) / sizeof(writes[0]), writes, 0, nullptr);

}

void FxDeferredGPass::Create(FxDeferredRenderer* renderer, const Vec2u& extent)
{
    mRendererInst = renderer;
    mGPassPipeline = &mRendererInst->GPassPipeline;

    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, RendererFramesInFlight);
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    DescriptorPool.Create(Renderer->GetDevice(), RendererFramesInFlight);

    DepthAttachment.Create(
        extent,
        VK_FORMAT_D16_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    PositionsAttachment.Create(
        extent,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    ColorAttachment.Create(
        extent,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );


    FxSizedArray image_views = {
        ColorAttachment.View,
        PositionsAttachment.View,
        DepthAttachment.View
    };

    Framebuffer.Create(image_views, renderer->GPassPipeline, extent);

    UniformBuffer.Create(1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    UniformBuffer.Map();

    BuildDescriptorSets();
}

void FxDeferredGPass::Begin()
{
    RvkFrameData* frame = Renderer->GetFrame();


    VkClearValue clear_values[] = {
        // Albedo
        VkClearValue {
            .color = { { 1.0f, 0.8f, 0.7f, 1.0f } }
        },
        // Positions
        VkClearValue {
            .color = { { 0.0f, 0.0f, 0.0f, 0.0f } }
        },
        VkClearValue {
            .depthStencil = { 1.0f, 0 }
        }
    };


    mGPassPipeline->RenderPass.Begin(Framebuffer.Framebuffer, FxMakeSlice(clear_values, FxSizeofArray(clear_values)));
    mGPassPipeline->Bind(frame->CommandBuffer);
}

void FxDeferredGPass::Submit()
{
    RvkFrameData *frame = Renderer->GetFrame();

    const VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->ImageAvailable.Semaphore,
        .pWaitDstStageMask = wait_stages,
        // command buffers
        .commandBufferCount = 1,
        .pCommandBuffers = &frame->CommandBuffer.CommandBuffer,
        // signal semaphores
        .signalSemaphoreCount = 1,
        // .pSignalSemaphores = &frame->RenderFinished.Semaphore
        .pSignalSemaphores = &frame->OffscreenSem.Semaphore
    };

    VkTry(
        vkQueueSubmit(Renderer->GetDevice()->GraphicsQueue, 1, &submit_info, VK_NULL_HANDLE),
        "Error submitting draw buffer"
    );
}

void FxDeferredGPass::End()
{
    mGPassPipeline->RenderPass.End();
}

void FxDeferredGPass::SubmitUniforms(const RvkUniformBufferObject& ubo)
{
    memcpy(UniformBuffer.MappedBuffer, &ubo, sizeof(RvkUniformBufferObject));
}

void FxDeferredGPass::Destroy()
{
    if (mGPassPipeline == nullptr) {
        return;
    }

    DepthAttachment.Destroy();
    PositionsAttachment.Destroy();
    ColorAttachment.Destroy();

    DescriptorSet.Destroy();
    DescriptorPool.Destroy();

    Framebuffer.Destroy();

    UniformBuffer.UnMap();
    UniformBuffer.Destroy();

    mGPassPipeline = nullptr;

    // Renderer->GPassDescriptorPool.Destroy();
}
