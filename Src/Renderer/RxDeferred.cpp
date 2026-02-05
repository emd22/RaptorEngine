#include "RxDeferred.hpp"

#include "Backend/RxDsLayoutBuilder.hpp"
#include "Backend/RxShader.hpp"
#include "FxEngine.hpp"

#include <FxObjectManager.hpp>
#include <Renderer/FxCamera.hpp>
#include <Renderer/RxDeferred.hpp>
#include <Renderer/RxPipelineBuilder.hpp>
#include <Renderer/RxRenderBackend.hpp>


FX_SET_MODULE_NAME("DeferredRenderer")

void RxDeferredRenderer::Create(const FxVec2u& extent)
{
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8);
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4);
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2);
    DescriptorPool.Create(gRenderer->GetDevice(), 10);

    CreateGPassPipeline();
    CreateLightingPipeline();
    CreateCompPipeline();

    // CreateUnlitPipeline();
}

void RxDeferredRenderer::Destroy()
{
    DestroyCompPipeline();
    DestroyGPassPipeline();
    DestroyLightingPipeline();
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

    {
        RxDsLayoutBuilder builder {};
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
        builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
        // builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RxShaderType::eVertex);
        DsLayoutGPassMaterial = builder.Build();
    }

    {
        RxDsLayoutBuilder builder {};
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
        DsLayoutGPassMaterialAlbedoOnly = builder.Build();
    }

    FxStackArray<VkDescriptorSetLayout, 3> ds_layouts = {
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

    VkPipelineLayout layout = RxPipeline::CreateLayout(FxSlice(push_consts), FxSlice(ds_layouts));

    RxUtil::SetDebugLabel("Geometry Pipeline Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}

VkPipelineLayout RxDeferredRenderer::CreateUnlitPipelineLayout()
{
    {
        RxDsLayoutBuilder builder {};
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
        DsLayoutUnlit = builder.Build();
    }

    FxStackArray<VkDescriptorSetLayout, 3> ds_layouts = {
        DsLayoutUnlit,
        DsLayoutLightingMaterialProperties,
        gObjectManager->DsLayoutObjectBuffer,
    };

    FxStackArray<RxPushConstants, 1> push_consts = {
        RxPushConstants {
            .Size = sizeof(FxDrawPushConstants),
            .StageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkPipelineLayout layout = RxPipeline::CreateLayout(FxSlice(push_consts), FxSlice(ds_layouts));

    RxUtil::SetDebugLabel("Unlit Pipeline Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}

void RxDeferredRenderer::CreateUnlitPipeline()
{
    VkPipelineLayout layout = CreateUnlitPipelineLayout();

    RxShader shader_unlit("Unlit");
    FxRef<RxShaderProgram> vertex_shader = shader_unlit.GetProgram(RxShaderType::eVertex, {});
    FxRef<RxShaderProgram> fragment_shader = shader_unlit.GetProgram(RxShaderType::eFragment, {});

    // RxAttachment* target = LightPass.GetTarget(RxImageFormat::eRGBA16_Float);
    // FxAssert(target != nullptr);

    FxVertexInfo vertex_info = FxMakeVertexInfo();

    RxPipelineBuilder builder {};

    RxAttachmentList attachment_list {};
    // attachment_list.Add(*target);

    builder.SetLayout(layout)
        .SetName("Unlit Pipeline")
        .AddBlendAttachment({ .Enabled = false })
        .SetAttachments(&attachment_list)
        .SetShaders(vertex_shader, fragment_shader)
        .SetRenderPass(&LightPass.GetRenderPass())
        .SetVertexInfo(&vertex_info)
        .SetCullMode(VK_CULL_MODE_BACK_BIT)
        .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);
    builder.Build(PlUnlit);
}


void RxDeferredRenderer::CreateGPassPipeline()
{
    VkPipelineLayout gpass_layout = CreateGPassPipelineLayout();
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

    builder.SetLayout(gpass_layout)
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
    if (DsLayoutGPassMaterialAlbedoOnly) {
        vkDestroyDescriptorSetLayout(device, DsLayoutGPassMaterialAlbedoOnly, nullptr);
        DsLayoutGPassMaterialAlbedoOnly = nullptr;
    }

    PlGeometry.Destroy();

    PlGeometryWithNormalMaps.Layout = nullptr;
    PlGeometryWithNormalMaps.Destroy();

    PlGeometryWireframe.Layout = nullptr;
    PlGeometryWireframe.Destroy();
}


void RxDeferredRenderer::CreateLightingDSLayout()
{
    // Fragment DS

    RxDsLayoutBuilder builder {};

    // sDepth
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
    // sAlbedo
    builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
    // sNormal
    builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);
    // sShadowDepth
    builder.AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RxShaderType::eFragment);

    builder.AddBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, RxShaderType::eFragment);

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
    if (DsLayoutLightingFrag == nullptr) {
        CreateLightingDSLayout();
    }

    LightPass.Create(gRenderer->Swapchain.Extent);
    LightPass.AddTarget(RxImageFormat::eRGBA16_Float, RxAttachment::scFullScreen,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, RxImageAspectFlag::eColor);
    LightPass.BuildRenderStage();

    DsLighting.Create(DescriptorPool, DsLayoutLightingFrag);

    // sDepth
    DsLighting.AddImageFromTarget(0, GPass.GetTarget(RxImageFormat::eD32_Float), &gRenderer->Swapchain.DepthSampler);
    // sAlbedo
    DsLighting.AddImageFromTarget(1, GPass.GetTarget(RxImageFormat::eBGRA8_UNorm), &gRenderer->Swapchain.ColorSampler);
    // sNormals
    DsLighting.AddImageFromTarget(2, GPass.GetTarget(RxImageFormat::eRGBA16_Float),
                                  &gRenderer->Swapchain.NormalsSampler);
    // Skip 3 for the shadow target, added by RxDirectionalShadows
    // Uniform buffer
    DsLighting.AddBuffer(4, &gRenderer->Uniforms.GetGpuBuffer(), 0, gRenderer->Uniforms.scUniformBufferSize);

    DsLighting.Build();

    RxShader lighting_shader("Lighting");

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
            .SetAttachments(&LightPass.GetTargets())
            .SetShaders(vertex_shader, fragment_shader)
            .SetRenderPass(&LightPass.GetRenderPass())
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
            .SetAttachments(&LightPass.GetTargets())
            .SetShaders(vertex_shader, fragment_shader)
            .SetRenderPass(&LightPass.GetRenderPass())
            .SetVertexInfo(nullptr)
            .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

        builder.Build(PlLightingDirectional);
    }
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

    PlLightingOutsideVolume.Destroy();

    PlLightingInsideVolume.Layout = nullptr;
    PlLightingInsideVolume.Destroy();
}

//////////////////////////////////////////
// RxDeferredRenderer CompPass Functions
//////////////////////////////////////////

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
    VkPipelineLayout comp_layout = CreateCompPipelineLayout();

    CreateCompPass();


    RxShader shader_composition("Composition");

    FxRef<RxShaderProgram> lit_vertex_shader = shader_composition.GetProgram(RxShaderType::eVertex, {});
    FxRef<RxShaderProgram> lit_fragment_shader = shader_composition.GetProgram(RxShaderType::eFragment, {});

    RxPipelineBuilder builder;

    builder.SetLayout(comp_layout)
        .SetName("Composition Pipeline")
        .AddBlendAttachment({ .Enabled = false })
        .SetAttachments(&CompPass.GetTargets())
        .SetShaders(lit_vertex_shader, lit_fragment_shader)
        .SetRenderPass(&CompPass.GetRenderPass())
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

void RxDeferredRenderer::CreateCompPass()
{
    DsComposition.Create(DescriptorPool, DsLayoutCompFrag);

    CompPass.Create(gRenderer->Swapchain.Extent);

    CompPass.ClearValues.Insert(VkClearValue { .color = { { 0.0f, 0.3f, 0.0f, 1.0f } } });

    CompPass.MarkFinalStage();
    CompPass.BuildRenderStage();

    DsComposition.AddImageFromTarget(1, GPass.GetTarget(RxImageFormat::eD32_Float), &gRenderer->Swapchain.DepthSampler);

    DsComposition.AddImageFromTarget(2, GPass.GetTarget(RxImageFormat::eBGRA8_UNorm),
                                     &gRenderer->Swapchain.ColorSampler);

    DsComposition.AddImageFromTarget(3, GPass.GetTarget(RxImageFormat::eRGBA16_Float),
                                     &gRenderer->Swapchain.NormalsSampler);

    DsComposition.AddImageFromTarget(4, LightPass.GetTarget(RxImageFormat::eRGBA16_Float),
                                     &gRenderer->Swapchain.LightsSampler);


    DsComposition.Build();
}

void RxDeferredRenderer::DoCompPass(FxCamera& camera)
{
    RxCommandBuffer& cmd = gRenderer->GetFrame()->CommandBuffer;

    FxCompositionPushConstants push_constants {};
    memcpy(push_constants.ViewInverse, camera.InvViewMatrix.RawData, sizeof(FxMat4f));
    memcpy(push_constants.ProjInverse, camera.InvProjectionMatrix.RawData, sizeof(FxMat4f));


    vkCmdPushConstants(cmd.Get(), PlComposition.Layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
                       &push_constants);

    // CompPass.Begin(cmd, PlComposition);

    DsComposition.Bind(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PlComposition);

    // Use single triangle instead of two triangles as it removes the overlapping quads the gpu
    // renders between triangles. Source: https://wallisc.github.io/rendering/2021/04/18/Fullscreen-Pass.html
    vkCmdDraw(cmd.CommandBuffer, 3, 1, 0, 0);

    CompPass.End();

    cmd.End();
}
