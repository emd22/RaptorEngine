#include "RxDeferred.hpp"

#include "Backend/RxShader.hpp"
#include "Backend/ShaderList.hpp"
#include "Renderer.hpp"

#include <Renderer/RxDeferred.hpp>

FX_SET_MODULE_NAME("DeferredRenderer")

void RxDeferredRenderer::Create(const FxVec2u& extent)
{
    CreateGPassPipeline();
    CreateCompPipeline();
    CreateLightingPipeline();

    GPasses.InitSize(RendererFramesInFlight);
    LightingPasses.InitSize(RendererFramesInFlight);
    CompPasses.InitSize(RendererFramesInFlight);

    FxSizedArray<VkImageView> temp_views;
    temp_views.InitSize(1);

    OutputFramebuffers.InitSize(RendererFramesInFlight);

    for (int frame_index = 0; frame_index < RendererFramesInFlight; frame_index++) {
        GPasses[frame_index].Create(this, extent);

        LightingPasses[frame_index].Create(this, frame_index, extent);
        // Pass in the current frame index to map to the swapchain's output images
        CompPasses[frame_index].Create(this, frame_index, extent);

        temp_views[0] = Renderer->Swapchain.OutputImages[frame_index].View;
        OutputFramebuffers[frame_index].Create(temp_views, CompPipeline, RpComposition, extent);
    }
}

void RxDeferredRenderer::Destroy()
{
    if (OutputFramebuffers.IsEmpty()) {
        return;
    }

    for (int i = 0; i < RendererFramesInFlight; i++) {
        GPasses[i].Destroy();
        LightingPasses[i].Destroy();
        CompPasses[i].Destroy();
        OutputFramebuffers[i].Destroy();
    }

    DestroyCompPipeline();
    DestroyGPassPipeline();
    DestroyLightingPipeline();

    RpGeometry.Destroy();
    RpLighting.Destroy();
    RpComposition.Destroy();

    GPasses.Free();
    LightingPasses.Free();
    CompPasses.Free();
    OutputFramebuffers.Free();
}

/////////////////////////////////////
// FxRenderer GPass Functions
/////////////////////////////////////

RxDeferredGPass* RxDeferredRenderer::GetCurrentGPass() { return &GPasses[Renderer->GetFrameNumber()]; }

VkPipelineLayout RxDeferredRenderer::CreateGPassPipelineLayout()
{
    RxGpuDevice* device = Renderer->GetDevice();

    // Material properties buffer DS

    {
        VkDescriptorSetLayoutBinding material_properties_binding {
            .binding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutCreateInfo ds_material_info { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                           .bindingCount = 1,
                                                           .pBindings = &material_properties_binding };

        VkResult status = vkCreateDescriptorSetLayout(device->Device, &ds_material_info, nullptr,
                                                      &DsLayoutLightingMaterialProperties);
        if (status != VK_SUCCESS) {
            FxModulePanicVulkan("Failed to create pipeline descriptor set layout", status);
        }
    }

    // Vertex DS
    //    {
    //        VkDescriptorSetLayoutBinding ubo_layout_binding {
    //            .binding = 0,
    //            .descriptorCount = 1,
    //            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    //            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    //            .pImmutableSamplers = nullptr,
    //        };
    //
    //        VkDescriptorSetLayoutCreateInfo ds_layout_info {
    //            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    //            .bindingCount = 1,
    //            .pBindings = &ubo_layout_binding,
    //        };
    //
    //        VkResult status = vkCreateDescriptorSetLayout(device->Device,
    //        &ds_layout_info, nullptr, &DsLayoutGPassVertex); if (status !=
    //        VK_SUCCESS) {
    //            FxModulePanic("Failed to create pipeline descriptor set layout",
    //            status);
    //        }
    //    }

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
            FxModulePanicVulkan("Failed to create pipeline descriptor set layout", status);
        }
    }

    VkDescriptorSetLayout layouts[] = {
        DsLayoutGPassMaterial, DsLayoutLightingMaterialProperties,
        //        DsLayoutGPassVertex,
    };

    VkPipelineLayout layout = GPassPipeline.CreateLayout(sizeof(FxDrawPushConstants), 0,
                                                         FxMakeSlice(layouts, FxSizeofArray(layouts)));

    RxUtil::SetDebugLabel("Geometry Pipeline Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}

void RxDeferredRenderer::CreateGPassPipeline()
{
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = {
        // Color
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        },
        // Normals
        VkPipelineColorBlendAttachmentState {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
            .blendEnable = VK_FALSE,
        },
    };

    VkAttachmentDescription color_attachments_list[] = {
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
        // Normals output
        VkAttachmentDescription {
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },

        VkAttachmentDescription {
            .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        },
    };

    ShaderList shader_list;

    RxShader vertex_shader("../shaders/main.vert.spv", RxShaderType::Vertex);
    RxShader fragment_shader("../shaders/main.frag.spv", RxShaderType::Fragment);

    shader_list.Vertex = vertex_shader.ShaderModule;
    shader_list.Fragment = fragment_shader.ShaderModule;

    // Create the layout for the GPass pipeline
    CreateGPassPipelineLayout();

    FxVertexInfo vert_info = FxMakeVertexInfo();

    const FxSlice<VkAttachmentDescription> color_attachments = FxMakeSlice(color_attachments_list,
                                                                           FxSizeofArray(color_attachments_list));

    RpGeometry.Create2(color_attachments);

    GPassPipeline.Create("Geometry", shader_list, color_attachments,
                         FxMakeSlice(color_blend_attachments, FxSizeofArray(color_blend_attachments)), &vert_info,
                         RpGeometry,
                         { .CullMode = VK_CULL_MODE_BACK_BIT, .WindingOrder = VK_FRONT_FACE_COUNTER_CLOCKWISE });
}

void RxDeferredRenderer::DestroyGPassPipeline()
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

////////////////////////////////////////////////
// RxDeferredRenderer LightingPass Functions
////////////////////////////////////////////////

RxDeferredLightingPass* RxDeferredRenderer::GetCurrentLightingPass()
{
    return &LightingPasses[Renderer->GetFrameNumber()];
}

void RxDeferredRenderer::CreateLightingDSLayout()
{
    RxGpuDevice* device = Renderer->GetDevice();

    // Fragment DS

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

    VkDescriptorSetLayoutBinding normals_layout_binding {
        .binding = 3,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr,
    };

    VkDescriptorSetLayoutBinding bindings[] = { positions_layout_binding, albedo_layout_binding,
                                                normals_layout_binding };

    VkDescriptorSetLayoutCreateInfo lighting_layout_info {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = FxSizeofArray(bindings),
        .pBindings = bindings,
    };

    VkResult status;

    status = vkCreateDescriptorSetLayout(device->Device, &lighting_layout_info, nullptr, &DsLayoutLightingFrag);
    if (status != VK_SUCCESS) {
        FxModulePanicVulkan("Failed to create pipeline descriptor set layout", status);
    }
}

VkPipelineLayout RxDeferredRenderer::CreateLightingPipelineLayout()
{
    VkDescriptorSetLayout layouts[] = {
        DsLayoutLightingFrag,
        DsLayoutLightingMaterialProperties,
    };

    VkPipelineLayout layout = LightingPipeline.CreateLayout(sizeof(FxLightVertPushConstants), // Vertex push constants
                                                            sizeof(FxLightFragPushConstants), // Fragment push constants
                                                            FxMakeSlice(layouts, FxSizeofArray(layouts)));

    RxUtil::SetDebugLabel("Lighting Pipeline Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}

void RxDeferredRenderer::CreateLightingPipeline()
{
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = { VkPipelineColorBlendAttachmentState {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_TRUE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    } };

    VkAttachmentDescription color_attachments_list[] = {
        // Combined output
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
    };

    ShaderList shader_list;

    RxShader vertex_shader("../shaders/lighting.vert.spv", RxShaderType::Vertex);
    RxShader fragment_shader("../shaders/lighting.frag.spv", RxShaderType::Fragment);

    shader_list.Vertex = vertex_shader.ShaderModule;
    shader_list.Fragment = fragment_shader.ShaderModule;

    if (DsLayoutLightingFrag == nullptr) {
        CreateLightingDSLayout();
    }

    CreateLightingPipelineLayout();

    FxVertexInfo vertex_info = FxMakeLightVertexInfo();

    const FxSlice<VkAttachmentDescription> color_attachments = FxMakeSlice(color_attachments_list,
                                                                           FxSizeofArray(color_attachments_list));

    RpLighting.Create2(color_attachments);

    LightingPipeline.Create("Lighting", shader_list, color_attachments,
                            FxMakeSlice(color_blend_attachments, FxSizeofArray(color_blend_attachments)), &vertex_info,
                            RpLighting,
                            { .CullMode = VK_CULL_MODE_FRONT_BIT, .WindingOrder = VK_FRONT_FACE_CLOCKWISE });
}

void RxDeferredRenderer::RebuildLightingPipeline()
{
    RxGraphicsPipeline old_pipeline = LightingPipeline;
    CreateLightingPipeline();
    old_pipeline.Destroy();
}

void RxDeferredRenderer::DestroyLightingPipeline()
{
    VkDevice device = Renderer->GetDevice()->Device;

    // Destroy descriptor set layouts
    if (DsLayoutLightingFrag) {
        vkDestroyDescriptorSetLayout(device, DsLayoutLightingFrag, nullptr);
        DsLayoutLightingFrag = nullptr;
    }

    if (DsLayoutLightingMaterialProperties) {
        vkDestroyDescriptorSetLayout(device, DsLayoutLightingMaterialProperties, nullptr);
        DsLayoutLightingMaterialProperties = nullptr;
    }


    LightingPipeline.Destroy();
}

//////////////////////////////////////////
// RxDeferredRenderer CompPass Functions
//////////////////////////////////////////

RxDeferredCompPass* RxDeferredRenderer::GetCurrentCompPass() { return &CompPasses[Renderer->GetFrameNumber()]; }

VkPipelineLayout RxDeferredRenderer::CreateCompPipelineLayout()
{
    RxGpuDevice* device = Renderer->GetDevice();

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

        VkDescriptorSetLayoutBinding normals_layout_binding {
            .binding = 3,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutBinding lights_layout_binding {
            .binding = 4,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        };

        VkDescriptorSetLayoutBinding bindings[] = { positions_layout_binding, albedo_layout_binding,
                                                    normals_layout_binding, lights_layout_binding };

        VkDescriptorSetLayoutCreateInfo comp_layout_info {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = FxSizeofArray(bindings),
            .pBindings = bindings,
        };

        VkResult status;
        status = vkCreateDescriptorSetLayout(device->Device, &comp_layout_info, nullptr, &DsLayoutCompFrag);
        if (status != VK_SUCCESS) {
            FxModulePanicVulkan("Failed to create pipeline descriptor set layout", status);
        }
    }

    VkDescriptorSetLayout layouts[] = {
        DsLayoutCompFrag,
    };

    VkPipelineLayout layout = CompPipeline.CreateLayout(0, sizeof(FxCompositionPushConstants),
                                                        FxMakeSlice(layouts, FxSizeofArray(layouts)));
    RxUtil::SetDebugLabel("Composition Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}

void RxDeferredRenderer::CreateCompPipeline()
{
    VkPipelineColorBlendAttachmentState color_blend_attachments[] = { VkPipelineColorBlendAttachmentState {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_FALSE,
    } };

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

    RxShader vertex_shader("../shaders/composition.vert.spv", RxShaderType::Vertex);
    RxShader fragment_shader("../shaders/composition.frag.spv", RxShaderType::Fragment);

    shader_list.Vertex = vertex_shader.ShaderModule;
    shader_list.Fragment = fragment_shader.ShaderModule;

    CreateCompPipelineLayout();

    FxSlice<VkAttachmentDescription> color_attachments = FxMakeSlice(attachments, FxSizeofArray(attachments));

    RpComposition.Create2(color_attachments);

    CompPipeline.Create("Composition", shader_list, color_attachments,
                        FxMakeSlice(color_blend_attachments, FxSizeofArray(color_blend_attachments)), nullptr,
                        RpComposition, { .CullMode = VK_CULL_MODE_NONE, .WindingOrder = VK_FRONT_FACE_CLOCKWISE });
}

void RxDeferredRenderer::DestroyCompPipeline()
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
// RxDeferredGPass Functions
/////////////////////////////////////

void RxDeferredGPass::BuildDescriptorSets()
{
    //    DescriptorSet.Create(DescriptorPool,
    //    mRendererInst->DsLayoutGPassVertex);
    //
    //    VkDescriptorBufferInfo ubo_info {
    //        .buffer = UniformBuffer.Buffer,
    //        .offset = 0,
    //        .range = sizeof(RxUniformBufferObject)
    //    };
    //
    //    VkWriteDescriptorSet ubo_write {
    //        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    //        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    //        .descriptorCount = 1,
    //        .dstSet = DescriptorSet,
    //        .dstBinding = 0,
    //        .dstArrayElement = 0,
    //        .pBufferInfo = &ubo_info,
    //    };
    //
    //    const VkWriteDescriptorSet writes[] = { ubo_write };
    //
    //    vkUpdateDescriptorSets(Renderer->GetDevice()->Device, sizeof(writes) /
    //    sizeof(writes[0]), writes, 0, nullptr);
}

void RxDeferredGPass::Create(RxDeferredRenderer* renderer, const FxVec2u& extent)
{
    mRendererInst = renderer;
    mRenderPass = &mRendererInst->RpGeometry;
    mGPassPipeline = &mRendererInst->GPassPipeline;


    //    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    //    RendererFramesInFlight);
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, RendererFramesInFlight);
    DescriptorPool.Create(Renderer->GetDevice(), RendererFramesInFlight);

    DepthAttachment.Create(RxImageType::Image2D, extent, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_IMAGE_TILING_OPTIMAL,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT);

    ColorAttachment.Create(RxImageType::Image2D, extent, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    NormalsAttachment.Create(RxImageType::Image2D, extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT);

    FxSizedArray image_views = { ColorAttachment.View, NormalsAttachment.View, DepthAttachment.View };

    Framebuffer.Create(image_views, renderer->GPassPipeline, *mRenderPass, extent);

    //    UniformBuffer.Create(1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    //    VMA_MEMORY_USAGE_CPU_TO_GPU); UniformBuffer.Map();

    BuildDescriptorSets();
}

void RxDeferredGPass::Begin()
{
    RxFrameData* frame = Renderer->GetFrame();

    VkClearValue clear_values[] = {
        // Albedo
        VkClearValue { .color = { { 1.0f, 0.8f, 0.7f, 1.0f } } },
        // Normals
        VkClearValue { .color = { { 0.0f, 0.0f, 0.0f, 0.0f } } },
        VkClearValue { .depthStencil = { 0.0f, 0 } },
    };

    mRenderPass->Begin(&frame->CommandBuffer, Framebuffer.Framebuffer,
                       FxMakeSlice(clear_values, FxSizeofArray(clear_values)));
    mGPassPipeline->Bind(frame->CommandBuffer);
}

void RxDeferredGPass::End() { mRenderPass->End(); }

void RxDeferredGPass::Submit()
{
    RxFrameData* frame = Renderer->GetFrame();

    const VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

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
        .pSignalSemaphores = &frame->OffscreenSem.Semaphore,
    };

    VkTry(vkQueueSubmit(Renderer->GetDevice()->GraphicsQueue, 1, &submit_info, VK_NULL_HANDLE),
          "Error submitting draw buffer");
}

void RxDeferredGPass::SubmitUniforms(const RxUniformBufferObject& ubo)
{
    memcpy(UniformBuffer.MappedBuffer, &ubo, sizeof(RxUniformBufferObject));
}

void RxDeferredGPass::Destroy()
{
    if (mGPassPipeline == nullptr) {
        return;
    }

    DepthAttachment.Destroy();
    ColorAttachment.Destroy();

    DescriptorPool.Destroy();

    Framebuffer.Destroy();

    UniformBuffer.UnMap();
    UniformBuffer.Destroy();

    mGPassPipeline = nullptr;
}

/////////////////////////////////////
// RxDeferredLightingPass Functions
/////////////////////////////////////

void RxDeferredLightingPass::Create(RxDeferredRenderer* renderer, uint16 frame_index, const FxVec2u& extent)
{
    mRendererInst = renderer;
    mRenderPass = &mRendererInst->RpLighting;
    mLightingPipeline = &mRendererInst->LightingPipeline;

    // Albedo sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    // Positions sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    // Normals sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    DescriptorPool.Create(Renderer->GetDevice(), RendererFramesInFlight);

    ColorAttachment.Create(RxImageType::Image2D, extent, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    FxSizedArray image_views = { ColorAttachment.View };

    Framebuffer.Create(image_views, renderer->LightingPipeline, *mRenderPass, extent);

    BuildDescriptorSets(frame_index);
}

void RxDeferredLightingPass::Begin()
{
    RxFrameData* frame = Renderer->GetFrame();

    VkClearValue clear_values[] = {
        // Output color
        VkClearValue { .color = { { 0.0f, 0.0f, 0.0f, 0.0f } } },
    };

    mRenderPass->Begin(&frame->LightCommandBuffer, Framebuffer.Framebuffer,
                       FxMakeSlice(clear_values, FxSizeofArray(clear_values)));
    mLightingPipeline->Bind(frame->LightCommandBuffer);

    DescriptorSet.Bind(frame->LightCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *mLightingPipeline);
}

void RxDeferredLightingPass::End() { mRenderPass->End(); }

void RxDeferredLightingPass::Submit()
{
    RxFrameData* frame = Renderer->GetFrame();

    const VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->OffscreenSem.Semaphore,

        .pWaitDstStageMask = wait_stages,
        // command buffers
        .commandBufferCount = 1,
        .pCommandBuffers = &frame->LightCommandBuffer.CommandBuffer,
        // signal semaphores
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &frame->LightingSem.Semaphore,
    };

    VkTry(vkQueueSubmit(Renderer->GetDevice()->GraphicsQueue, 1, &submit_info, VK_NULL_HANDLE),
          "Error submitting draw buffer");
}

void RxDeferredLightingPass::BuildDescriptorSets(uint16 frame_index)
{
    DescriptorSet.Create(DescriptorPool, mRendererInst->DsLayoutLightingFrag);

    RxDeferredGPass& gpass = mRendererInst->GPasses[frame_index];

    FxStackArray<VkWriteDescriptorSet, 3> write_infos;

    // Depth image descriptor
    {
        const int binding_index = 1;

        VkDescriptorImageInfo positions_image_info { .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                     .imageView = gpass.DepthAttachment.View,
                                                     .sampler = Renderer->Swapchain.DepthSampler.Sampler };

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

        VkDescriptorImageInfo color_image_info { .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                 .imageView = gpass.ColorAttachment.View,
                                                 .sampler = Renderer->Swapchain.ColorSampler.Sampler };

        VkWriteDescriptorSet color_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .pImageInfo = &color_image_info,

        };

        write_infos.Insert(color_write);
    }

    // Normals image descriptor
    {
        const int binding_index = 3;

        VkDescriptorImageInfo normals_image_info {
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = gpass.NormalsAttachment.View,
            .sampler = Renderer->Swapchain.NormalsSampler.Sampler,
        };

        VkWriteDescriptorSet normals_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .pImageInfo = &normals_image_info,

        };

        write_infos.Insert(normals_write);
    }

    vkUpdateDescriptorSets(Renderer->GetDevice()->Device, write_infos.Size, write_infos.Data, 0, nullptr);
}

void RxDeferredLightingPass::Destroy()
{
    if (mLightingPipeline == nullptr) {
        return;
    }

    ColorAttachment.Destroy();
    DescriptorPool.Destroy();
    Framebuffer.Destroy();

    mLightingPipeline = nullptr;
}

/////////////////////////////////////////
// RxDeferredCompPass Functions
/////////////////////////////////////////

void RxDeferredCompPass::Create(RxDeferredRenderer* renderer, uint16 frame_index, const FxVec2u& extent)
{
    FxAssert(Renderer->Swapchain.Initialized == true);

    mRendererInst = renderer;
    mCompPipeline = &mRendererInst->CompPipeline;
    mRenderPass = &mRendererInst->RpComposition;

    // Albedo sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    // Positions sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    // Normals sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    // Lights sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RendererFramesInFlight);
    DescriptorPool.Create(Renderer->GetDevice(), RendererFramesInFlight);

    // The output image renders to the swapchains output
    OutputImage = &Renderer->Swapchain.OutputImages[frame_index];

    FxSizedArray<VkImageView> image_views = { OutputImage->View };

    Framebuffer.Create(image_views, renderer->CompPipeline, *mRenderPass, extent);

    BuildDescriptorSets(frame_index);
}

void RxDeferredCompPass::BuildDescriptorSets(uint16 frame_index)
{
    DescriptorSet.Create(DescriptorPool, mRendererInst->DsLayoutCompFrag);

    RxDeferredGPass& gpass = mRendererInst->GPasses[frame_index];

    FxStackArray<VkWriteDescriptorSet, 4> write_infos;

    // Depth image descriptor
    {
        const int binding_index = 1;

        VkDescriptorImageInfo positions_image_info { .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                     .imageView = gpass.DepthAttachment.View,
                                                     .sampler = Renderer->Swapchain.DepthSampler.Sampler };

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

        VkDescriptorImageInfo color_image_info { .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                 .imageView = gpass.ColorAttachment.View,
                                                 .sampler = Renderer->Swapchain.ColorSampler.Sampler };

        VkWriteDescriptorSet color_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .pImageInfo = &color_image_info,

        };

        write_infos.Insert(color_write);
    }

    // Normals image descriptor
    {
        const int binding_index = 3;

        VkDescriptorImageInfo normals_image_info { .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                   .imageView = gpass.NormalsAttachment.View,
                                                   .sampler = Renderer->Swapchain.NormalsSampler.Sampler };

        VkWriteDescriptorSet normals_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .pImageInfo = &normals_image_info,

        };

        write_infos.Insert(normals_write);
    }

    // Lights image descriptor
    {
        const int binding_index = 4;

        RxDeferredLightingPass& light_pass = mRendererInst->LightingPasses[frame_index];

        VkDescriptorImageInfo lights_image_info { .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                  .imageView = light_pass.ColorAttachment.View,
                                                  .sampler = Renderer->Swapchain.LightsSampler.Sampler };

        VkWriteDescriptorSet lights_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .pImageInfo = &lights_image_info,

        };

        write_infos.Insert(lights_write);
    }

    vkUpdateDescriptorSets(Renderer->GetDevice()->Device, write_infos.Size, write_infos.Data, 0, nullptr);
}

void RxDeferredCompPass::Begin()
{
    mCurrentFrame = Renderer->GetFrame();
    mCurrentFrame->CompCommandBuffer.Reset();
    mCurrentFrame->CompCommandBuffer.Record();
}

#include <Renderer/FxCamera.hpp>

void RxDeferredCompPass::DoCompPass(FxCamera& render_cam)
{
    FxCompositionPushConstants push_constants {};
    memcpy(push_constants.ViewInverse, render_cam.InvViewMatrix.RawData, sizeof(FxMat4f));
    memcpy(push_constants.ProjInverse, render_cam.InvProjectionMatrix.RawData, sizeof(FxMat4f));

    vkCmdPushConstants(mCurrentFrame->CompCommandBuffer.CommandBuffer, mCompPipeline->Layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants), &push_constants);

    VkClearValue clear_values[] = {
        // Output colour
        VkClearValue { .color = { { 0.0f, 0.0f, 0.0f, 0.0f } } },
    };

    FxSlice<VkClearValue> slice(clear_values, FxSizeofArray(clear_values));

    RxCommandBuffer& cmd = mCurrentFrame->CompCommandBuffer;

    VkFramebuffer framebuffer = mRendererInst->OutputFramebuffers[Renderer->GetImageIndex()].Framebuffer;
    mRenderPass->Begin(&mCurrentFrame->CompCommandBuffer, framebuffer, slice);

    mCompPipeline->Bind(cmd);
    DescriptorSet.Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mRendererInst->CompPipeline);

    vkCmdDraw(cmd.CommandBuffer, 3, 1, 0, 0);

    mRenderPass->End();

    cmd.End();
}

void RxDeferredCompPass::Destroy()
{
    if (mCompPipeline == nullptr) {
        return;
    }

    DescriptorPool.Destroy();
    Framebuffer.Destroy();

    mCompPipeline = nullptr;
}
