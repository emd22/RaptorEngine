#include "FxDeferred.hpp"
#include "Renderer.hpp"

#include "Backend/ShaderList.hpp"
#include "Backend/RvkShader.hpp"

#include <Renderer/FxDeferred.hpp>

FX_SET_MODULE_NAME("DeferredRenderer")

void FxDeferredRenderer::Create(const Vec2u& extent)
{
    CreateGPassPipeline();
    CreateCompPipeline();

    GPasses.InitSize(RendererFramesInFlight);
    CompPasses.InitSize(RendererFramesInFlight);

    FxSizedArray<VkImageView> temp_views;
    temp_views.InitSize(1);

    OutputFramebuffers.InitSize(RendererFramesInFlight);


    for (int frame_index = 0; frame_index < RendererFramesInFlight; frame_index++) {
        GPasses[frame_index].Create(this, extent);

        // Pass in the current frame index to map to the swapchain's output images
        CompPasses[frame_index].Create(this, frame_index, extent);

        temp_views[0] = Renderer->Swapchain.OutputImages[frame_index].View;
        OutputFramebuffers[frame_index].Create(temp_views, CompPipeline, extent);
    }
}

void FxDeferredRenderer::Destroy()
{
    if (OutputFramebuffers.IsEmpty()) {
        return;
    }

    for (int i = 0; i < RendererFramesInFlight; i++) {
        GPasses[i].Destroy();
        CompPasses[i].Destroy();
        OutputFramebuffers[i].Destroy();
    }

    DestroyCompPipeline();
    DestroyGPassPipeline();

    GPasses.Free();
    CompPasses.Free();
    OutputFramebuffers.Free();
}

/////////////////////////////////////
// FxRenderer GPass Functions
/////////////////////////////////////


FxDeferredGPass* FxDeferredRenderer::GetCurrentGPass()
{
    return &GPasses[Renderer->GetFrameNumber()];
}

VkPipelineLayout FxDeferredRenderer::CreateGPassPipelineLayout()
{
    RvkGpuDevice* device = Renderer->GetDevice();

    // Vertex DS
    {
        VkDescriptorSetLayoutBinding ubo_layout_binding {
            .binding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutCreateInfo ds_layout_info {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &ubo_layout_binding,
        };

        VkResult status = vkCreateDescriptorSetLayout(device->Device, &ds_layout_info, nullptr, &DsLayoutGPassVertex);
        if (status != VK_SUCCESS) {
            FxModulePanic("Failed to create pipeline descriptor set layout", status);
        }
    }

    // Fragment DS
    {
        // Albedo texture
        VkDescriptorSetLayoutBinding albedo_layout_binding {
            .binding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutCreateInfo ds_layout_info {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &albedo_layout_binding,
        };

        VkResult status = vkCreateDescriptorSetLayout(device->Device, &ds_layout_info, nullptr, &DsLayoutGPassMaterial);
        if (status != VK_SUCCESS) {
            FxModulePanic("Failed to create pipeline descriptor set layout", status);
        }
    }

    VkDescriptorSetLayout layouts[] = {
        DsLayoutGPassVertex,
        DsLayoutGPassMaterial,
    };

    return GPassPipeline.CreateLayout(sizeof(FxDrawPushConstants), FxMakeSlice(layouts, FxSizeofArray(layouts)));
}

void FxDeferredRenderer::CreateGPassPipeline()
{
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        // Color
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                            | VK_COLOR_COMPONENT_G_BIT
                            | VK_COLOR_COMPONENT_B_BIT
                            | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        },
        // Positions
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                            | VK_COLOR_COMPONENT_G_BIT
                            | VK_COLOR_COMPONENT_B_BIT
                            | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        },
        // Normals
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                            | VK_COLOR_COMPONENT_G_BIT
                            | VK_COLOR_COMPONENT_B_BIT
                            | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        },
    };

    VkAttachmentDescription attachments[] = {
        // Albedo output
        VkAttachmentDescription {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        // Positions output
        VkAttachmentDescription {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        // Normals output
        VkAttachmentDescription {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },

        VkAttachmentDescription {
            .format = VK_FORMAT_D16_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        }
    };

    ShaderList shader_list;

    RvkShader vertex_shader("../shaders/main.vert.spv", RvkShaderType::Vertex);
    RvkShader fragment_shader("../shaders/main.frag.spv", RvkShaderType::Fragment);

    shader_list.Vertex = vertex_shader.ShaderModule;
    shader_list.Fragment = fragment_shader.ShaderModule;

    VkPipelineLayout layout = CreateGPassPipelineLayout();

    FxVertexInfo vert_info = FxMakeVertexInfo();

    GPassPipeline.Create(
        shader_list,
        layout,
        FxMakeSlice(attachments, FxSizeofArray(attachments)),
        FxMakeSlice(color_blend_attachments, FxSizeofArray(color_blend_attachments)),
        &vert_info
    );
}

void FxDeferredRenderer::DestroyGPassPipeline()
{
    VkDevice device = Renderer->GetDevice()->Device;

    // Destroy descriptor set layouts
    if (DsLayoutGPassVertex) {
        vkDestroyDescriptorSetLayout(device, DsLayoutGPassVertex, nullptr);
        DsLayoutGPassVertex = nullptr;
    }
    if (DsLayoutGPassMaterial) {
        vkDestroyDescriptorSetLayout(device, DsLayoutGPassMaterial, nullptr);
        DsLayoutGPassMaterial = nullptr;
    }

    GPassPipeline.Destroy();
}

//////////////////////////////////////////
// FxDeferredRenderer CompPass Functions
//////////////////////////////////////////

FxDeferredCompPass* FxDeferredRenderer::GetCurrentCompPass()
{
    return &CompPasses[Renderer->GetFrameNumber()];
}

VkPipelineLayout FxDeferredRenderer::CreateCompPipelineLayout()
{
    RvkGpuDevice* device = Renderer->GetDevice();

    {
        VkDescriptorSetLayoutBinding positions_layout_binding {
            .binding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutBinding albedo_layout_binding {
            .binding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutBinding bindings[] = {
            positions_layout_binding,
            albedo_layout_binding
        };

        VkDescriptorSetLayoutCreateInfo comp_layout_info {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = FxSizeofArray(bindings),
            .pBindings = bindings,
        };

        VkResult status;
        status = vkCreateDescriptorSetLayout(device->Device, &comp_layout_info, nullptr, &DsLayoutCompFrag);
        if (status != VK_SUCCESS) {
            FxModulePanic("Failed to create pipeline descriptor set layout", status);
        }
    }

    VkDescriptorSetLayout layouts[] = {
        DsLayoutCompFrag
    };

    return CompPipeline.CreateLayout(0, FxMakeSlice(layouts, FxSizeofArray(layouts)));
}


void FxDeferredRenderer::CreateCompPipeline()
{
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                            | VK_COLOR_COMPONENT_G_BIT
                            | VK_COLOR_COMPONENT_B_BIT
                            | VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        }
    };

    VkAttachmentDescription attachments[] = {
        // Combined output
        VkAttachmentDescription {
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        },
    };

    ShaderList shader_list;

    RvkShader vertex_shader("../shaders/composition.vert.spv", RvkShaderType::Vertex);
    RvkShader fragment_shader("../shaders/composition.frag.spv", RvkShaderType::Fragment);

    shader_list.Vertex = vertex_shader.ShaderModule;
    shader_list.Fragment = fragment_shader.ShaderModule;

    VkPipelineLayout layout = CreateCompPipelineLayout();

    CompPipeline.Create(
        shader_list,
        layout,
        FxMakeSlice(attachments, FxSizeofArray(attachments)),
        FxMakeSlice(color_blend_attachments, FxSizeofArray(color_blend_attachments)),
        nullptr
    );
}


void FxDeferredRenderer::DestroyCompPipeline()
{
    VkDevice device = Renderer->GetDevice()->Device;

    // Destroy descriptor set layouts
    if (DsLayoutCompFrag) {
        vkDestroyDescriptorSetLayout(device, DsLayoutCompFrag, nullptr);
        DsLayoutCompFrag = nullptr;
    }

    CompPipeline.Destroy();
}





/////////////////////////////////////
// FxDeferredGPass Functions
/////////////////////////////////////


void FxDeferredGPass::BuildDescriptorSets()
{
    DescriptorSet.Create(DescriptorPool, mRendererInst->DsLayoutGPassVertex);

    VkDescriptorBufferInfo ubo_info {
        .buffer = UniformBuffer.Buffer,
        .offset = 0,
        .range = sizeof(RvkUniformBufferObject)
    };

    VkWriteDescriptorSet ubo_write {
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

    ColorAttachment.Create(
        extent,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    PositionsAttachment.Create(
        extent,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    NormalsAttachment.Create(
        extent,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT
    );

    FxSizedArray image_views = {
        ColorAttachment.View,
        PositionsAttachment.View,
        NormalsAttachment.View,

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
        // Normals
        VkClearValue {
            .color = { { 0.0f, 0.0f, 0.0f, 0.0f } }
        },
        VkClearValue {
            .depthStencil = { 1.0f, 0 }
        }
    };


    mGPassPipeline->RenderPass.Begin(&frame->CommandBuffer, Framebuffer.Framebuffer, FxMakeSlice(clear_values, FxSizeofArray(clear_values)));
    mGPassPipeline->Bind(frame->CommandBuffer);
}

void FxDeferredGPass::End()
{
    mGPassPipeline->RenderPass.End();
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

    DescriptorPool.Destroy();

    Framebuffer.Destroy();

    UniformBuffer.UnMap();
    UniformBuffer.Destroy();

    mGPassPipeline = nullptr;
}


/////////////////////////////////////////
// FxDeferredCompPass Functions
/////////////////////////////////////////


void FxDeferredCompPass::Create(FxDeferredRenderer* renderer, uint16 frame_index, const Vec2u& extent)
{
    FxAssert(Renderer->Swapchain.Initialized == true);

    mRendererInst = renderer;
    mCompPipeline = &mRendererInst->CompPipeline;

    // Albedo sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    // Positions sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    DescriptorPool.Create(Renderer->GetDevice(), RendererFramesInFlight);

    // The output image renders to the swapchains output
    OutputImage = &Renderer->Swapchain.OutputImages[frame_index];

    FxSizedArray<VkImageView> image_views = {
        OutputImage->View
    };

    Framebuffer.Create(image_views, renderer->CompPipeline, extent);



    BuildDescriptorSets(frame_index);
}


void FxDeferredCompPass::BuildDescriptorSets(uint16 frame_index)
{
    DescriptorSet.Create(DescriptorPool, mRendererInst->DsLayoutCompFrag);

    FxDeferredGPass& gpass = mRendererInst->GPasses[frame_index];

    FxStackArray<VkWriteDescriptorSet, 3> write_infos;

    // Positions image descriptor
    {
        const int binding_index = 1;

        VkDescriptorImageInfo positions_image_info {
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = gpass.PositionsAttachment.View,
            .sampler = Renderer->Swapchain.PositionSampler.Sampler
        };

        VkWriteDescriptorSet positions_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .pImageInfo = &positions_image_info,

        };

        write_infos.Insert(positions_write);
    }

    // Color image descriptor
    {
        const int binding_index = 2;

        VkDescriptorImageInfo positions_image_info {
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = gpass.ColorAttachment.View,
            .sampler = Renderer->Swapchain.ColorSampler.Sampler
        };

        VkWriteDescriptorSet positions_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .pImageInfo = &positions_image_info,

        };

        write_infos.Insert(positions_write);
    }

    vkUpdateDescriptorSets(Renderer->GetDevice()->Device, write_infos.Size, write_infos.Data, 0, nullptr);
}

void FxDeferredCompPass::Begin()
{
    mCurrentFrame = Renderer->GetFrame();
    mCurrentFrame->CompCommandBuffer.Reset();
    mCurrentFrame->CompCommandBuffer.Record();
}

void FxDeferredCompPass::DoCompPass()
{
    VkClearValue clear_values[] = {
        // Albedo
        VkClearValue {
            .color = { { 1.0f, 0.8f, 0.7f, 1.0f } }
        },
    };

    FxSlice<VkClearValue> slice(clear_values, FxSizeofArray(clear_values));

    RvkCommandBuffer& cmd = mCurrentFrame->CompCommandBuffer;

    VkFramebuffer framebuffer = mRendererInst->OutputFramebuffers[Renderer->GetImageIndex()].Framebuffer;
    mCompPipeline->RenderPass.Begin(&mCurrentFrame->CompCommandBuffer, framebuffer, slice);

    mCompPipeline->Bind(cmd);
    DescriptorSet.Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mRendererInst->CompPipeline);

    vkCmdDraw(cmd.CommandBuffer, 3, 1, 0, 0);

    mCompPipeline->RenderPass.End();

    cmd.End();
}

void FxDeferredCompPass::Destroy()
{
    if (mCompPipeline == nullptr) {
        return;
    }

    DescriptorPool.Destroy();
    Framebuffer.Destroy();

    mCompPipeline = nullptr;
}
