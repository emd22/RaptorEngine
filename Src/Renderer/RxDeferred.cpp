#include "RxDeferred.hpp"

#include "Backend/RxDsLayoutBuilder.hpp"
#include "Backend/RxShader.hpp"
#include "FxEngine.hpp"

#include <FxObjectManager.hpp>
#include <Renderer/RxDeferred.hpp>
#include <Renderer/RxPipelineBuilder.hpp>
#include <Renderer/RxRenderBackend.hpp>


FX_SET_MODULE_NAME("DeferredRenderer")

void RxDeferredRenderer::Create(const FxVec2u& extent)
{
    CreateGPassPipeline();
    CreateCompPipeline();
    CreateLightingPipeline();

    GPasses.InitSize(RxFramesInFlight);
    LightingPasses.InitSize(RxFramesInFlight);
    CompPasses.InitSize(RxFramesInFlight);

    FxSizedArray<VkImageView> temp_views;
    temp_views.InitSize(1);

    OutputFramebuffers.InitSize(RxFramesInFlight);

    for (int frame_index = 0; frame_index < RxFramesInFlight; frame_index++) {
        // GPasses[frame_index].Create(this, extent);

        LightingPasses[frame_index].Create(this, frame_index, extent);
        // Pass in the current frame index to map to the swapchain's output images
        CompPasses[frame_index].Create(this, frame_index, extent);

        temp_views[0] = gRenderer->Swapchain.OutputImages[frame_index].View;
        OutputFramebuffers[frame_index].Create(temp_views, RpComposition, extent);
    }
}

void RxDeferredRenderer::Destroy()
{
    if (OutputFramebuffers.IsEmpty()) {
        return;
    }

    for (int i = 0; i < RxFramesInFlight; i++) {
        // GPasses[i].Destroy();
        LightingPasses[i].Destroy();
        CompPasses[i].Destroy();
        OutputFramebuffers[i].Destroy();
    }

    DestroyCompPipeline();
    DestroyGPassPipeline();
    DestroyLightingPipeline();

    // RpGeometry.Destroy();
    RpLighting.Destroy();
    RpComposition.Destroy();

    GPasses.Free();
    LightingPasses.Free();
    CompPasses.Free();
    OutputFramebuffers.Free();
}

void RxDeferredRenderer::ToggleWireframe(bool enable)
{
    // RxPipeline* new_pipeline = &PlGeometry;
    // if (enable) {
    //     new_pipeline = &PlGeometryWireframe;
    //     FxLogInfo("Enabling wireframe");
    // }
    // else {
    //     FxLogInfo("Disabling wireframe");
    // }

    // pGeometryPipeline = new_pipeline;

    // for (RxDeferredGPass& gpass : GPasses) {
    //     gpass.mPlGeometry = new_pipeline;
    // }
}

void RxDeferredRenderer::CreateGPass()
{
    GPass.Create(gRenderer->Swapchain.Extent);

    // Albedo target
    GPass.AddTarget(RxImageFormat::eBGRA8_UNorm, RxAttachment::scFullScreen,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, RxImageAspectFlag::eColor);

    // Normals target
    GPass.AddTarget(RxImageFormat::eRGBA16_Float, RxAttachment::scFullScreen,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, RxImageAspectFlag::eColor);

    // Depth target
    GPass.AddTarget(RxImageFormat::eD32_Float, RxAttachment::scFullScreen,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    RxImageAspectFlag::eDepth);

    GPass.BuildRenderStage();
}

/////////////////////////////////////
// FxRenderer GPass Functions
/////////////////////////////////////

RxDeferredGPass* RxDeferredRenderer::GetCurrentGPass() { return &GPasses[gRenderer->GetFrameNumber()]; }

VkPipelineLayout RxDeferredRenderer::CreateGPassPipelineLayout()
{
    // Material descriptor set
    {
        // Create material properties DS layout
        RxDsLayoutBuilder builder {};
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RxShaderType::eFragment);
        DsLayoutLightingMaterialProperties = builder.Build();
    }

    {
        // Create object buffer DS layout
    }

    // Fragment descriptor set
    {
        RxDsLayoutBuilder builder {};
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
        // builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RxShaderType::eVertex);
        DsLayoutGPassMaterial = builder.Build();
    }

    FxStackArray<VkDescriptorSetLayout, 3> layouts = {
        DsLayoutGPassMaterial,
        DsLayoutLightingMaterialProperties,
        gObjectManager->DsLayoutObjectBuffer,
    };

    FxStackArray<RxPushConstants, 1> push_consts = {
        RxPushConstants {
            .Size = sizeof(FxDrawPushConstants),
            .StageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    FxSlice<VkDescriptorSetLayout> layouts2 = FxSlice<VkDescriptorSetLayout>(layouts);
    FxLogWarning("Layouts: {}", layouts2.Size);

    VkPipelineLayout layout = RxPipeline::CreateLayout(FxSlice(push_consts), FxSlice(layouts));

    RxUtil::SetDebugLabel("Geometry Pipeline Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}


void RxDeferredRenderer::CreateGPassPipeline()
{
    // RxAttachmentList attachments;

    // attachments
    //     .Add(RxAttachment(RxImageFormat::eBGRA8_UNorm, RxAttachment::scFullScreen))  // Albedo
    //     .Add(RxAttachment(RxImageFormat::eRGBA16_Float, RxAttachment::scFullScreen)) // Normals
    //     .Add(RxAttachment(RxImageFormat::eD32_Float, RxAttachment::scFullScreen));   // Depth

    CreateGPass();


    RxShader shader_geometry("Geometry");
    FxRef<RxShaderProgram> vertex_shader = shader_geometry.GetProgram(RxShaderType::eVertex, {});
    FxRef<RxShaderProgram> fragment_shader = shader_geometry.GetProgram(RxShaderType::eFragment, {});

    FxVertexInfo vertex_info = FxMakeVertexInfo();

    // RpGeometry.Create(attachments, gRenderer->Swapchain.Extent);

    RxPipelineBuilder builder;

    builder.SetLayout(CreateGPassPipelineLayout())
        .SetName("Geometry Pipeline")
        .AddBlendAttachment({ .Enabled = false })
        .AddBlendAttachment({ .Enabled = false })
        .SetAttachments(&GPass.GetTargets())
        .SetShaders(vertex_shader, fragment_shader)
        .SetRenderPass(&GPass.GetRenderPass())
        .SetVertexInfo(&vertex_info)
        .SetCullMode(VK_CULL_MODE_BACK_BIT)
        .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

    builder.SetPolygonMode(VK_POLYGON_MODE_FILL).Build(PlGeometry);
    builder.SetPolygonMode(VK_POLYGON_MODE_LINE).Build(PlGeometryWireframe);

    // Create geometry pipeline with normal maps
    {
        FxSizedArray<FxShaderMacro> normal_mapped_macros { FxShaderMacro { "USE_NORMAL_MAPS", "1" } };

        FxRef<RxShaderProgram> nm_vertex_shader = shader_geometry.GetProgram(RxShaderType::eVertex,
                                                                             normal_mapped_macros);

        FxRef<RxShaderProgram> nm_fragment_shader = shader_geometry.GetProgram(RxShaderType::eFragment,
                                                                               normal_mapped_macros);

        builder.SetPolygonMode(VK_POLYGON_MODE_FILL)
            .SetShaders(nm_vertex_shader, nm_fragment_shader)
            .Build(PlGeometryWithNormalMaps);
    }

    pGeometryPipeline = &PlGeometry;
}

void RxDeferredRenderer::DestroyGPassPipeline()
{
    VkDevice device = gRenderer->GetDevice()->Device;

    // Destroy descriptor set layouts
    if (DsLayoutGPassVertex) {
        vkDestroyDescriptorSetLayout(device, DsLayoutGPassVertex, nullptr);
        DsLayoutGPassVertex = nullptr;
    }
    if (DsLayoutGPassMaterial) {
        vkDestroyDescriptorSetLayout(device, DsLayoutGPassMaterial, nullptr);
        DsLayoutGPassMaterial = nullptr;
    }

    PlGeometry.Destroy();

    PlGeometryWithNormalMaps.Layout = nullptr;
    PlGeometryWithNormalMaps.Destroy();

    PlGeometryWireframe.Layout = nullptr;
    PlGeometryWireframe.Destroy();
}

////////////////////////////////////////////////
// RxDeferredRenderer LightingPass Functions
////////////////////////////////////////////////

RxDeferredLightingPass* RxDeferredRenderer::GetCurrentLightingPass()
{
    return &LightingPasses[gRenderer->GetFrameNumber()];
}

void RxDeferredRenderer::CreateLightingDSLayout()
{
    // Fragment DS

    RxDsLayoutBuilder builder {};

    // sDepth
    builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
    // sAlbedo
    builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
    // sNormal
    builder.AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
    // sShadowDepth
    builder.AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);

    builder.AddBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, RxShaderType::eFragment);

    DsLayoutLightingFrag = builder.Build();
}

VkPipelineLayout RxDeferredRenderer::CreateLightingPipelineLayout()
{
    VkDescriptorSetLayout layouts[] = {
        DsLayoutLightingFrag,
        DsLayoutLightingMaterialProperties,
        gObjectManager->DsLayoutObjectBuffer,
    };

    FxStackArray<RxPushConstants, 2> push_consts = {
        RxPushConstants { .Size = sizeof(FxLightVertPushConstants), .StageFlags = VK_SHADER_STAGE_VERTEX_BIT },
        // RxPushConstants { .Size = sizeof(FxLightFragPushConstants), .StageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
    };

    VkPipelineLayout layout = RxPipeline::CreateLayout(FxSlice(push_consts),
                                                       FxMakeSlice(layouts, FxSizeofArray(layouts)));
    RxUtil::SetDebugLabel("Lighting Pipeline Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    PlLightingOutsideVolume.SetLayout(layout);
    PlLightingInsideVolume.SetLayout(layout);

    return layout;
}

void RxDeferredRenderer::CreateLightingPipeline()
{
    // VkPipelineColorBlendAttachmentState color_blend_attachments[] = { VkPipelineColorBlendAttachmentState {
    //     .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
    //                       VK_COLOR_COMPONENT_A_BIT,
    //     .blendEnable = VK_TRUE,
    //     .alphaBlendOp = VK_BLEND_OP_ADD,
    //     .colorBlendOp = VK_BLEND_OP_ADD,
    //     .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    //     .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    //     .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    //     .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    // // } };

    // VkAttachmentDescription color_attachments_list[] = {
    //     // Combined output
    //     VkAttachmentDescription {
    //         .format = VK_FORMAT_R16G16B16A16_SFLOAT,
    //         .samples = VK_SAMPLE_COUNT_1_BIT,
    //         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    //         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    //         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    //         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    //         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    //         .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    //     },
    // };

    // ShaderList shader_list;

    // RxShader vertex_shader("../Shaders/Spirv/Lighting.spv_vs", RxShaderType::Vertex);
    // RxShader fragment_shader("../Shaders/Spirv/Lighting.spv_fs", RxShaderType::Fragment);

    // shader_list.Vertex = vertex_shader.ShaderModule;
    // shader_list.Fragment = fragment_shader.ShaderModule;

    // RxShader raw_shader_list[2] = { vertex_shader, fragment_shader };
    // FxSlice<RxShader> shader_list = FxMakeSlice<RxShader>(raw_shader_list, 2);
    //

    if (DsLayoutLightingFrag == nullptr) {
        CreateLightingDSLayout();
    }


    // const FxSlice<VkAttachmentDescription> color_attachments = FxMakeSlice(color_attachments_list,
    //                                                                        FxSizeofArray(color_attachments_list));

    RxAttachmentList attachment_list;
    attachment_list.Add(RxAttachment(RxImageFormat::eRGBA16_Float, RxAttachment::scFullScreen));

    RpLighting.Create(attachment_list, gRenderer->Swapchain.Extent);

    RxShader lighting_shader("Lighting");

    // PlLightingOutsideVolume.Create("Lighting(Inside Volume)", shader_list, color_attachments,
    //                                FxMakeSlice(color_blend_attachments, FxSizeofArray(color_blend_attachments)),
    //                                &vertex_info, RpLighting,
    //                                { .CullMode = VK_CULL_MODE_FRONT_BIT, .WindingOrder = VK_FRONT_FACE_CLOCKWISE });

    // PlLightingInsideVolume.Create("Lighting (Outside Volume)", shader_list, color_attachments,
    //                               FxMakeSlice(color_blend_attachments, FxSizeofArray(color_blend_attachments)),
    //                               &vertex_info, RpLighting,
    //                               { .CullMode = VK_CULL_MODE_BACK_BIT, .WindingOrder = VK_FRONT_FACE_CLOCKWISE });

    {
        FxRef<RxShaderProgram> vertex_shader = lighting_shader.GetProgram(RxShaderType::eVertex, {});
        FxRef<RxShaderProgram> fragment_shader = lighting_shader.GetProgram(RxShaderType::eFragment, {});

        FxVertexInfo vertex_info = FxMakeLightVertexInfo();

        RxPipelineBuilder builder {};
        builder.SetLayout(CreateLightingPipelineLayout())
            .SetName("Lighting(Point)")
            .AddBlendAttachment({
                .Enabled = true,
                .AlphaBlend { .Ops {
                    .Src = VK_BLEND_FACTOR_ONE,
                    .Dst = VK_BLEND_FACTOR_ZERO,
                } },
                .ColorBlend { .Ops { .Src = VK_BLEND_FACTOR_SRC_ALPHA, .Dst = VK_BLEND_FACTOR_ONE } },
            })
            .SetAttachments(&attachment_list)
            .SetShaders(vertex_shader, fragment_shader)
            .SetRenderPass(&RpLighting)
            .SetVertexInfo(&vertex_info)
            .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

        builder.SetCullMode(VK_CULL_MODE_BACK_BIT).Build(PlLightingOutsideVolume);
        builder.SetCullMode(VK_CULL_MODE_BACK_BIT).Build(PlLightingInsideVolume);
    }
    {
        FxSizedArray<FxShaderMacro> directional_macros { FxShaderMacro { "FX_LIGHT_DIRECTIONAL", "1" } };

        FxRef<RxShaderProgram> vertex_shader = lighting_shader.GetProgram(RxShaderType::eVertex, directional_macros);
        FxRef<RxShaderProgram> fragment_shader = lighting_shader.GetProgram(RxShaderType::eFragment,
                                                                            directional_macros);

        RxPipelineBuilder builder {};

        builder.SetLayout(CreateLightingPipelineLayout())
            .SetName("Lighting(Directional)")
            .AddBlendAttachment({
                .Enabled = true,
                .AlphaBlend { .Ops {
                    .Src = VK_BLEND_FACTOR_ONE,
                    .Dst = VK_BLEND_FACTOR_ZERO,
                } },
                .ColorBlend { .Ops { .Src = VK_BLEND_FACTOR_SRC_ALPHA, .Dst = VK_BLEND_FACTOR_ONE } },
            })
            .SetAttachments(&attachment_list)
            .SetShaders(vertex_shader, fragment_shader)
            .SetRenderPass(&RpLighting)
            .SetVertexInfo(nullptr)
            .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

        builder.Build(PlLightingDirectional);
    }
}

void RxDeferredRenderer::RebuildLightingPipeline()
{
    // RxGraphicsPipeline old_pipeline = PlLightingInsideVolume;
    // CreateLightingPipeline();
    // old_pipeline.Destroy();
}

void RxDeferredRenderer::DestroyLightingPipeline()
{
    VkDevice device = gRenderer->GetDevice()->Device;

    // Destroy descriptor set layouts
    if (DsLayoutLightingFrag) {
        vkDestroyDescriptorSetLayout(device, DsLayoutLightingFrag, nullptr);
        DsLayoutLightingFrag = nullptr;
    }

    if (DsLayoutLightingMaterialProperties) {
        vkDestroyDescriptorSetLayout(device, DsLayoutLightingMaterialProperties, nullptr);
        DsLayoutLightingMaterialProperties = nullptr;
    }

    // if (DsLayoutObjectBuffer) {
    //     vkDestroyDescriptorSetLayout(device, DsLayoutObjectBuffer, nullptr);
    //     DsLayoutObjectBuffer = nullptr;
    // }


    PlLightingOutsideVolume.Destroy();

    PlLightingInsideVolume.Layout = nullptr;
    PlLightingInsideVolume.Destroy();
}

//////////////////////////////////////////
// RxDeferredRenderer CompPass Functions
//////////////////////////////////////////

RxDeferredCompPass* RxDeferredRenderer::GetCurrentCompPass() { return &CompPasses[gRenderer->GetFrameNumber()]; }

VkPipelineLayout RxDeferredRenderer::CreateCompPipelineLayout()
{
    {
        RxDsLayoutBuilder builder {};

        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment)
            .AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment)
            .AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);

        DsLayoutCompFrag = builder.Build();
    }

    VkDescriptorSetLayout layouts[] = {
        DsLayoutCompFrag,
    };

    FxStackArray<RxPushConstants, 1> push_consts = { RxPushConstants { .Size = sizeof(FxCompositionPushConstants),
                                                                       .StageFlags = VK_SHADER_STAGE_FRAGMENT_BIT } };

    VkPipelineLayout layout = RxPipeline::CreateLayout(FxSlice(push_consts),
                                                       FxMakeSlice(layouts, FxSizeofArray(layouts)));

    RxUtil::SetDebugLabel("Composition Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);
    PlComposition.SetLayout(layout);

    return layout;
}

void RxDeferredRenderer::CreateCompPipeline()
{
    RxAttachmentList attachment_list;

    attachment_list.Add(RxAttachment(gRenderer->Swapchain.Surface.Format, FxVec2u::sZero, RxLoadOp::eDontCare,
                                     RxStoreOp::eStore, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));

    RpComposition.Create(attachment_list, gRenderer->Swapchain.Extent, FxVec2u::sZero);

    RxShader shader_composition("Composition");

    FxRef<RxShaderProgram> lit_vertex_shader = shader_composition.GetProgram(RxShaderType::eVertex, {});
    FxRef<RxShaderProgram> lit_fragment_shader = shader_composition.GetProgram(RxShaderType::eFragment, {});

    RxPipelineBuilder builder;

    builder.SetLayout(CreateCompPipelineLayout())
        .SetName("Composition Pipeline")
        .AddBlendAttachment({ .Enabled = false })
        .SetAttachments(&attachment_list)
        .SetShaders(lit_vertex_shader, lit_fragment_shader)
        .SetRenderPass(&RpComposition)
        .SetVertexInfo(nullptr)
        .SetCullMode(VK_CULL_MODE_NONE)
        .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

    builder.Build(PlComposition);


    FxSizedArray<FxShaderMacro> unlit_macros { FxShaderMacro { .pcName = "RENDER_UNLIT", .pcValue = "1" } };

    FxRef<RxShaderProgram> unlit_vertex_shader = shader_composition.GetProgram(RxShaderType::eVertex, unlit_macros);
    FxRef<RxShaderProgram> unlit_fragment_shader = shader_composition.GetProgram(RxShaderType::eFragment, unlit_macros);

    builder.SetShaders(unlit_vertex_shader, unlit_fragment_shader).Build(PlCompositionUnlit);
}

void RxDeferredRenderer::DestroyCompPipeline()
{
    VkDevice device = gRenderer->GetDevice()->Device;

    // Destroy descriptor set layouts
    if (DsLayoutCompFrag) {
        vkDestroyDescriptorSetLayout(device, DsLayoutCompFrag, nullptr);
        DsLayoutCompFrag = nullptr;
    }

    PlComposition.Destroy();
    PlCompositionUnlit.Layout = nullptr;
    PlCompositionUnlit.Destroy();
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
    //    vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, sizeof(writes) /
    //    sizeof(writes[0]), writes, 0, nullptr);
}

// void RxDeferredGPass::Create(RxDeferredRenderer* renderer, const FxVec2u& extent)
//{
// mRendererInst = renderer;

//// RxRenderPassCache::Handle rp_handle = gRenderPassCache->Request(mRendererInst->RpGeometryId);
//// mRenderPass = rp_handle.Item;
// mRenderPass = &mRendererInst->RpGeometry;
// mPlGeometry = &mRendererInst->PlGeometry;


////    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
////    RendererFramesInFlight);
// DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5);
// DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2);
// DescriptorPool.Create(gRenderer->GetDevice(), RxFramesInFlight);

// DepthAttachment.Create(RxImageType::e2d, extent, RxImageFormat::eD32_Float, VK_IMAGE_TILING_OPTIMAL,
//                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
//                        RxImageAspectFlag::eDepth);

// ColorAttachment.Create(RxImageType::e2d, extent, RxImageFormat::eBGRA8_UNorm, VK_IMAGE_TILING_OPTIMAL,
//                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, RxImageAspectFlag::eColor);

// NormalsAttachment.Create(RxImageType::e2d, extent, RxImageFormat::eRGBA16_Float, VK_IMAGE_TILING_OPTIMAL,
//                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
//                          RxImageAspectFlag::eColor);

// FxSizedArray image_views = { ColorAttachment.View, NormalsAttachment.View, DepthAttachment.View };

// Framebuffer.Create(image_views, *mRenderPass, extent);

////    UniformBuffer.Create(1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
////    VMA_MEMORY_USAGE_CPU_TO_GPU); UniformBuffer.Map();

// BuildDescriptorSets();
//}

// void RxDeferredGPass::Submit()
//{
//     RxFrameData* frame = gRenderer->GetFrame();
//
//     const VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT };
//
//     const VkSubmitInfo submit_info = {
//         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
//         .waitSemaphoreCount = 1,
//         .pWaitSemaphores = &frame->ShadowsSem.Semaphore,
//         .pWaitDstStageMask = wait_stages,
//         // command buffers
//         .commandBufferCount = 1,
//         .pCommandBuffers = &frame->CommandBuffer.CommandBuffer,
//         // signal semaphores
//         .signalSemaphoreCount = 1,
//         // .pSignalSemaphores = &frame->RenderFinished.Semaphore
//         .pSignalSemaphores = &frame->OffscreenSem.Semaphore,
//     };
//
//     VkTry(vkQueueSubmit(gRenderer->GetDevice()->GraphicsQueue, 1, &submit_info, VK_NULL_HANDLE),
//           "Error submitting draw buffer");
// }

/////////////////////////////////////
// RxDeferredLightingPass Functions
/////////////////////////////////////

void RxDeferredLightingPass::Create(RxDeferredRenderer* renderer, uint16 frame_index, const FxVec2u& extent)
{
    mRendererInst = renderer;
    mRenderPass = &mRendererInst->RpLighting;
    mPlLighting = &mRendererInst->PlLightingOutsideVolume;

    // Albedo sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5);

    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1);

    DescriptorPool.Create(gRenderer->GetDevice(), 4);

    ColorAttachment.Create(RxImageType::e2d, extent, RxImageFormat::eRGBA16_Float, VK_IMAGE_TILING_OPTIMAL,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, RxImageAspectFlag::eColor);

    FxSizedArray image_views = { ColorAttachment.View };

    Framebuffer.Create(image_views, *mRenderPass, extent);

    BuildDescriptorSets(frame_index);
}

void RxDeferredLightingPass::Begin()
{
    RxFrameData* frame = gRenderer->GetFrame();

    VkClearValue clear_values[] = {
        // Output color
        VkClearValue { .color = { { 0.0f, 0.0f, 0.0f, 0.0f } } },
    };

    mRenderPass->Begin(&frame->LightCommandBuffer, Framebuffer.Framebuffer,
                       FxMakeSlice(clear_values, FxSizeofArray(clear_values)));
    mPlLighting->Bind(frame->LightCommandBuffer);

    DescriptorSet.BindWithOffset(frame->LightCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *mPlLighting,
                                 gRenderer->Uniforms.GetBaseOffset());
}

void RxDeferredLightingPass::End() { mRenderPass->End(); }

void RxDeferredLightingPass::Submit()
{
    RxFrameData* frame = gRenderer->GetFrame();

    const VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    };

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame->OffscreenSem.Semaphore,

        .pWaitDstStageMask = wait_stages,

        // Command buffers
        .commandBufferCount = 1,
        .pCommandBuffers = &frame->LightCommandBuffer.CommandBuffer,

        // Signal semaphores
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &frame->LightingSem.Semaphore,
    };

    VkTry(vkQueueSubmit(gRenderer->GetDevice()->GraphicsQueue, 1, &submit_info, VK_NULL_HANDLE),
          "Error submitting draw buffer");
}

void RxDeferredLightingPass::BuildDescriptorSets(uint16 frame_index)
{
    DescriptorSet.Create(DescriptorPool, mRendererInst->DsLayoutLightingFrag);

    RxDeferredGPass& gpass = mRendererInst->GPasses[frame_index];


    FxStackArray<VkDescriptorImageInfo, 4> write_image_infos;
    FxStackArray<VkDescriptorBufferInfo, 2> write_buffer_infos;
    FxStackArray<VkWriteDescriptorSet, 6> write_infos;

    // Depth image descriptor
    {
        const int binding_index = 1;

        VkImageView gpass_depth_view = mRendererInst->GPass.GetTarget(RxImageFormat::eD32_Float)->Image.View;

        const VkDescriptorImageInfo positions_image_info {
            .sampler = gRenderer->Swapchain.DepthSampler.Sampler,
            .imageView = gpass_depth_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkWriteDescriptorSet positions_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = write_image_infos.Insert(positions_image_info),

        };

        write_infos.Insert(positions_write);
    }

    // Color image descriptor
    {
        const int binding_index = 2;

        VkImageView gpass_albedo_view = mRendererInst->GPass.GetTarget(RxImageFormat::eBGRA8_UNorm)->Image.View;

        const VkDescriptorImageInfo color_image_info {
            .sampler = gRenderer->Swapchain.ColorSampler.Sampler,
            .imageView = gpass_albedo_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkWriteDescriptorSet color_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = write_image_infos.Insert(color_image_info),

        };

        write_infos.Insert(color_write);
    }

    // Normals image descriptor
    {
        const int binding_index = 3;

        VkImageView gpass_normals_view = mRendererInst->GPass.GetTarget(RxImageFormat::eRGBA16_Float)->Image.View;

        const VkDescriptorImageInfo normals_image_info {
            .sampler = gRenderer->Swapchain.NormalsSampler.Sampler,
            .imageView = gpass_normals_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkWriteDescriptorSet normals_write { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                   .dstSet = DescriptorSet.Set,
                                                   .dstBinding = binding_index,
                                                   .dstArrayElement = 0,
                                                   .descriptorCount = 1,
                                                   .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                   .pImageInfo = write_image_infos.Insert(normals_image_info),
                                                   .pBufferInfo = nullptr };

        write_infos.Insert(normals_write);
    }

    // Note that the shadow texture is added in RxDirectionalShadow.hpp!

    // Uniform buffer
    {
        const int binding_index = 5;

        const VkDescriptorBufferInfo uniform_buffer_info { .buffer = gRenderer->Uniforms.GetGpuBuffer().Buffer,
                                                           .offset = 0,
                                                           .range = gRenderer->Uniforms.scUniformBufferSize };

        const VkWriteDescriptorSet uniform_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pImageInfo = nullptr,
            .pBufferInfo = write_buffer_infos.Insert(uniform_buffer_info),
        };

        write_infos.Insert(uniform_write);
    }


    // {
    //     VkDescriptorBufferInfo info {
    //         .buffer = gObjectManager->mObjectGpuBuffer.Buffer,
    //         .offset = 0,
    //         .range = VK_WHOLE_SIZE,
    //     };

    //     VkWriteDescriptorSet buffer_write {
    //         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    //         .dstSet = DescriptorSet.Set,
    //         .dstBinding = 0,
    //         .dstArrayElement = 0,
    //         .descriptorCount = 1,
    //         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    //         .pBufferInfo = write_buffer_infos.Insert(info),
    //     };

    //     write_infos.Insert(buffer_write);
    // }


    vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, write_infos.Size, write_infos.pData, 0, nullptr);
}

void RxDeferredLightingPass::Destroy()
{
    if (mPlLighting == nullptr) {
        return;
    }

    ColorAttachment.Destroy();
    DescriptorPool.Destroy();
    Framebuffer.Destroy();

    mPlLighting = nullptr;
}

/////////////////////////////////////////
// RxDeferredCompPass Functions
/////////////////////////////////////////

void RxDeferredCompPass::Create(RxDeferredRenderer* renderer, uint16 frame_index, const FxVec2u& extent)
{
    FxAssert(gRenderer->Swapchain.bInitialized == true);

    mRendererInst = renderer;
    mPlComposition = &mRendererInst->PlComposition;
    mRenderPass = &mRendererInst->RpComposition;

    // Albedo sampler
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5);
    DescriptorPool.Create(gRenderer->GetDevice(), RxFramesInFlight);

    // The output image renders to the swapchains output
    OutputImage = &gRenderer->Swapchain.OutputImages[frame_index];

    FxSizedArray<VkImageView> image_views = { OutputImage->View };

    Framebuffer.Create(image_views, *mRenderPass, extent);

    BuildDescriptorSets(frame_index);
}

void RxDeferredCompPass::BuildDescriptorSets(uint16 frame_index)
{
    DescriptorSet.Create(DescriptorPool, mRendererInst->DsLayoutCompFrag);

    RxDeferredGPass& gpass = mRendererInst->GPasses[frame_index];

    constexpr uint32 num_image_infos = 4;

    FxStackArray<VkDescriptorImageInfo, num_image_infos> write_image_infos;
    FxStackArray<VkWriteDescriptorSet, num_image_infos> write_infos;

    // Depth image descriptor
    {
        const int binding_index = 1;

        const VkDescriptorImageInfo positions_image_info {
            .sampler = gRenderer->Swapchain.DepthSampler.Sampler,
            .imageView = mRendererInst->GPass.GetTarget(RxImageFormat::eD32_Float)->Image.View,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkWriteDescriptorSet positions_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = write_image_infos.Insert(positions_image_info),

        };

        write_infos.Insert(positions_write);
    }

    // Color image descriptor
    {
        const int binding_index = 2;

        const VkDescriptorImageInfo color_image_info {
            .sampler = gRenderer->Swapchain.ColorSampler.Sampler,
            .imageView = mRendererInst->GPass.GetTarget(RxImageFormat::eBGRA8_UNorm)->Image.View,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkWriteDescriptorSet color_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = write_image_infos.Insert(color_image_info),
        };

        write_infos.Insert(color_write);
    }

    // Normals image descriptor
    {
        const int binding_index = 3;

        const VkDescriptorImageInfo normals_image_info {
            .sampler = gRenderer->Swapchain.NormalsSampler.Sampler,
            .imageView = mRendererInst->GPass.GetTarget(RxImageFormat::eRGBA16_Float)->Image.View,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkWriteDescriptorSet normals_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = write_image_infos.Insert(normals_image_info),
        };

        write_infos.Insert(normals_write);
    }

    // Lights image descriptor
    {
        const int binding_index = 4;

        RxDeferredLightingPass& light_pass = mRendererInst->LightingPasses[frame_index];

        const VkDescriptorImageInfo lights_image_info {
            .sampler = gRenderer->Swapchain.LightsSampler.Sampler,
            .imageView = light_pass.ColorAttachment.View,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        const VkWriteDescriptorSet lights_write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = DescriptorSet.Set,
            .dstBinding = binding_index,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = write_image_infos.Insert(lights_image_info),
        };

        write_infos.Insert(lights_write);
    }

    vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, write_infos.Size, write_infos.pData, 0, nullptr);
}

void RxDeferredCompPass::Begin()
{
    mCurrentFrame = gRenderer->GetFrame();
    mCurrentFrame->CompCommandBuffer.Reset();
    mCurrentFrame->CompCommandBuffer.Record();
}

#include <Renderer/FxCamera.hpp>

void RxDeferredCompPass::DoCompPass(FxCamera& render_cam)
{
    FxCompositionPushConstants push_constants {};
    memcpy(push_constants.ViewInverse, render_cam.InvViewMatrix.RawData, sizeof(FxMat4f));
    memcpy(push_constants.ProjInverse, render_cam.InvProjectionMatrix.RawData, sizeof(FxMat4f));

    vkCmdPushConstants(mCurrentFrame->CompCommandBuffer.CommandBuffer, mPlComposition->Layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants), &push_constants);

    VkClearValue clear_values[] = {
        // Output colour
        VkClearValue { .color = { { 0.0f, 0.3f, 0.0f, 1.0f } } },
    };

    FxSlice<VkClearValue> slice(clear_values, FxSizeofArray(clear_values));

    RxCommandBuffer& cmd = mCurrentFrame->CompCommandBuffer;

    VkFramebuffer framebuffer = mRendererInst->OutputFramebuffers[gRenderer->GetImageIndex()].Framebuffer;
    mRenderPass->Begin(&mCurrentFrame->CompCommandBuffer, framebuffer, slice);

    // mRendererInst->SkyboxRenderer.SkyboxAttachment = OutputImage;

    DescriptorSet.Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mRendererInst->PlComposition);
    mPlComposition->Bind(cmd);
    // Use single triangle instead of two triangles as it removes the overlapping quads the gpu
    // renders between triangles. Source: https://wallisc.github.io/rendering/2021/04/18/Fullscreen-Pass.html
    vkCmdDraw(cmd.CommandBuffer, 3, 1, 0, 0);

    mRenderPass->End();

    cmd.End();
}

void RxDeferredCompPass::Destroy()
{
    if (mPlComposition == nullptr) {
        return;
    }

    DescriptorPool.Destroy();
    Framebuffer.Destroy();

    mPlComposition = nullptr;
}
