#include "DeferredRenderer.hpp"

#include "Backend/DsLayoutBuilder.hpp"
#include "Backend/Shader.hpp"
#include "Backend/VertexDescription.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "Globals.hpp"
#include "PipelineBuilder.hpp"
#include "RenderBackend.hpp"
#include "ShaderCache.hpp"
#include "State.hpp"

#include <ObjectManager.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("DeferredRenderer")

void DeferredRenderer::Create(const Vec2u& extent)
{
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10);
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10);
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 10);
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5);
    DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10);
    DescriptorPool.Create(gRenderer->GetDevice(), 16);

    CreateGPassPipeline();
    CreateLightingPipeline();
    CreateCompPipeline();

    CreateUnlitPipeline();
}

void DeferredRenderer::Destroy()
{
    DestroyCompPipeline();
    DestroyGPassPipeline();
    DestroyLightingPipeline();
}

void DeferredRenderer::CreateGPass()
{
    GPass.Create(gRenderer->Swapchain.Extent);

    // Albedo target
    GPass.AddTarget(eImageFormat::BGRA8_UNorm, Target::scFullScreen,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

    // Normals target
    GPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

    // Depth target
    GPass.AddTarget(eImageFormat::eD32_Float, Target::scFullScreen,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Depth);

    GPass.BuildRenderStage();
}

/////////////////////////////////////
// Renderer GPass Functions
/////////////////////////////////////

VkPipelineLayout DeferredRenderer::CreateGPassPipelineLayout()
{
    // Material descriptor set
    {
        // Create material properties DS layout
        DsLayoutBuilder builder {};
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, eShaderType::Pixel);
        DsLayoutLightingMaterialProperties = builder.Build();
    }


    {
        DsLayoutBuilder builder {};
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
        builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
        // builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShaderType::Vertex);
        DsLayoutGPassMaterial = builder.Build();
    }

    {
        DsLayoutBuilder builder {};
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
        DsLayoutGPassMaterialAlbedoOnly = builder.Build();
    }

    StackArray<VkDescriptorSetLayout, 3> ds_layouts = {
        DsLayoutGPassMaterial,
        DsLayoutLightingMaterialProperties,
        gObjectManager->DsLayoutObjectBuffer,
    };

    StackArray<PushConstants, 1> push_consts = {
        PushConstants {
            .Size = sizeof(DrawPushConstants),
            .StageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkPipelineLayout layout = Pipeline::CreateLayout(Slice(push_consts), Slice(ds_layouts));

    Util::SetDebugLabel("Geometry PL L", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}


VkPipelineLayout DeferredRenderer::CreateGPassSkinnedPipelineLayout()
{
    {
        DsLayoutBuilder builder {};
        builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
        builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
        builder.AddBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, eShaderType::Vertex);
        DsLayoutGPassSkinned = builder.Build();
    }


    StackArray<VkDescriptorSetLayout, 3> ds_layouts = {
        DsLayoutGPassSkinned,
        DsLayoutLightingMaterialProperties,
        gObjectManager->DsLayoutObjectBuffer,
    };

    StackArray<PushConstants, 1> push_consts = {
        PushConstants {
            .Size = sizeof(DrawPushConstants),
            .StageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkPipelineLayout layout = Pipeline::CreateLayout(Slice(push_consts), Slice(ds_layouts));

    Util::SetDebugLabel("Geometry(Skinned) PL L", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}

VkPipelineLayout DeferredRenderer::CreateUnlitPipelineLayout()
{
    StackArray<VkDescriptorSetLayout, 3> ds_layouts = {
        DsLayoutGPassMaterial,
        DsLayoutLightingMaterialProperties,
        gObjectManager->DsLayoutObjectBuffer,
    };

    StackArray<PushConstants, 1> push_consts = {
        PushConstants {
            .Size = sizeof(DrawPushConstants),
            .StageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkPipelineLayout layout = Pipeline::CreateLayout(Slice(push_consts), Slice(ds_layouts));

    Util::SetDebugLabel("Unlit PL L", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}

VkPipelineLayout DeferredRenderer::CreateDebugLayerPipelineLayout()
{
    StackArray<VkDescriptorSetLayout, 1> ds_layouts = {};

    StackArray<PushConstants, 1> push_consts = {
        PushConstants {
            .Size = sizeof(DebugLayerPushConstants),
            .StageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        },
    };

    VkPipelineLayout layout = Pipeline::CreateLayout(Slice(push_consts), Slice(ds_layouts));

    Util::SetDebugLabel("Debug Layer PL L", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return layout;
}


void DeferredRenderer::CreateUnlitPipeline()
{
    VkPipelineLayout layout = CreateUnlitPipelineLayout();

    Ref<Shader> shader_unlit = gShaderCache->Request(eShaderName::Unlit);
    Ref<ShaderProgram> vertex_shader = shader_unlit->GetProgram(eShaderType::Vertex, {});
    Ref<ShaderProgram> pixel_shader = shader_unlit->GetProgram(eShaderType::Pixel, {});

    VertexDescription vertex_info = VertexUtil::BuildDescription<eVertexType::Default>();

    PipelineBuilder builder {};
    TargetList targets {};

    Target* lp_light_attachment = LightPass.GetTarget(eImageFormat::RGBA16_Float);
    Target* lp_depth_attachment = GPass.GetTarget(eImageFormat::eD32_Float);

    Assert(lp_light_attachment != nullptr && lp_depth_attachment != nullptr);

    Target depth_attachment(eImageFormat::eD32_Float, gRenderer->Swapchain.Extent);
    depth_attachment.LoadOp = eLoadOp::Load;
    depth_attachment.StoreOp = eStoreOp::DontCare;
    depth_attachment.Aspect = eImageAspectFlag::Depth;
    depth_attachment.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depth_attachment.InitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depth_attachment.SetImage(lp_depth_attachment->GetImage());
    targets.Add(depth_attachment);

    Target light_attachment(eImageFormat::RGBA16_Float, gRenderer->Swapchain.Extent);
    light_attachment.LoadOp = eLoadOp::Load;
    light_attachment.StoreOp = eStoreOp::Store;
    light_attachment.InitialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    light_attachment.FinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    light_attachment.SetImage(lp_light_attachment->GetImage());

    targets.Add(light_attachment);

    RpForward.Create(targets, Target::scFullScreen);
    FbForward.Create(targets.GetImageViews(), RpForward, Target::scFullScreen);

    builder.SetLayout(layout)
        .SetName("Unlit Pipeline")
        .AddBlendAttachment(0, { .Enabled = false })
        .SetOutputTargets(&targets)
        .SetShaders(vertex_shader, pixel_shader)
        .SetRenderPass(&RpForward)
        .SetVertexDescription(&vertex_info)
        .SetCullMode(VK_CULL_MODE_BACK_BIT)
        .SetWindingOrder(VK_FRONT_FACE_COUNTER_CLOCKWISE);
    builder.Build(PlUnlit);

    {
        Ref<Shader> shader_text = gShaderCache->Request(eShaderName::Text);
        vertex_shader = shader_text->GetProgram(eShaderType::Vertex, {});
        pixel_shader = shader_text->GetProgram(eShaderType::Pixel, {});

        builder.SetLayout(layout)
            .SetName("Text Pipeline")
            .AddBlendAttachment(0, { .Enabled = false })
            .SetOutputTargets(&targets)
            .SetShaders(vertex_shader, pixel_shader)
            .SetRenderPass(&RpForward)
            .SetVertexDescription(&vertex_info)
            .SetCullMode(VK_CULL_MODE_BACK_BIT)
            .SetWindingOrder(VK_FRONT_FACE_COUNTER_CLOCKWISE);
        builder.Build(PlText);
    }

    // Create debug layer pipeline

    VkPipelineLayout debug_layout = CreateDebugLayerPipelineLayout();

    vertex_shader = shader_unlit->GetProgram(eShaderType::Vertex,
                                             { ShaderMacro { .pcName = "IS_DEBUG_LAYER", .pcValue = "1" } });

    pixel_shader = shader_unlit->GetProgram(eShaderType::Pixel,
                                            { ShaderMacro { .pcName = "IS_DEBUG_LAYER", .pcValue = "1" } });


    VertexDescription debug_vertex_description = VertexUtil::BuildDescription<eVertexType::Slim>();

    builder.SetLayout(debug_layout)
        .SetName("Debug Layer Pipeline")
        .SetShaders(vertex_shader, pixel_shader)
        .SetWindingOrder(VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetVertexDescription(&debug_vertex_description)
        .SetProperties({ .bRenderLines = true })
        .Build(PlDebugLayer);
}


void DeferredRenderer::CreateGPassPipeline()
{
    VkPipelineLayout gpass_layout = CreateGPassPipelineLayout();

    CreateGPass();

    Ref<Shader> shader_geometry = gShaderCache->Request(eShaderName::Geometry);
    Ref<ShaderProgram> vertex_shader = shader_geometry->GetProgram(eShaderType::Vertex, {});
    Ref<ShaderProgram> fragment_shader = shader_geometry->GetProgram(eShaderType::Pixel, {});

    VertexDescription vertex_info = VertexUtil::BuildDescription<eVertexType::Default>();

    PipelineBuilder builder;

    builder.SetLayout(gpass_layout)
        .SetName("Geometry Pipeline")
        .AddBlendAttachment(0, { .Enabled = false })
        .AddBlendAttachment(1, { .Enabled = false })
        .SetOutputTargets(&GPass.GetTargets())
        .SetShaders(vertex_shader, fragment_shader)
        .SetRenderPass(&GPass.GetRenderPass())
        .SetVertexDescription(&vertex_info)
        .SetCullMode(VK_CULL_MODE_BACK_BIT)
        .SetWindingOrder(VK_FRONT_FACE_COUNTER_CLOCKWISE);

    builder.SetPolygonMode(VK_POLYGON_MODE_FILL).Build(PlGeometry);
    builder.SetPolygonMode(VK_POLYGON_MODE_LINE).Build(PlGeometryWireframe);

    // Create geometry pipeline with normal maps
    {
        SizedArray<ShaderMacro> normal_mapped_macros { ShaderMacro { "USE_NORMAL_MAPS", "1" } };

        Ref<ShaderProgram> nm_vertex_shader = shader_geometry->GetProgram(eShaderType::Vertex, normal_mapped_macros);

        Ref<ShaderProgram> nm_fragment_shader = shader_geometry->GetProgram(eShaderType::Pixel, normal_mapped_macros);

        builder.SetPolygonMode(VK_POLYGON_MODE_FILL)
            .SetShaders(nm_vertex_shader, nm_fragment_shader)
            .Build(PlGeometryWithNormalMaps);
    }

    {
        vertex_info = VertexUtil::BuildDescription<eVertexType::Skinned>();

        SizedArray<ShaderMacro> macros = { ShaderMacro { "USE_NORMAL_MAPS", "1" },
                                           ShaderMacro { "USE_SKINNING", "1" } };

        Ref<ShaderProgram> nm_vertex_shader = shader_geometry->GetProgram(eShaderType::Vertex, macros);
        Ref<ShaderProgram> nm_fragment_shader = shader_geometry->GetProgram(eShaderType::Pixel, macros);

        VkPipelineLayout skinned_layout = CreateGPassSkinnedPipelineLayout();

        builder.SetLayout(skinned_layout)
            .SetPolygonMode(VK_POLYGON_MODE_FILL)
            .SetVertexDescription(&vertex_info)
            .SetShaders(nm_vertex_shader, nm_fragment_shader)
            .Build(PlGeometrySkinned);
    }

    pGeometryPipeline = &PlGeometry;
}

void DeferredRenderer::DestroyGPassPipeline()
{
    VkDevice device = gRenderer->GetDevice()->Device;

    // Destroy descriptor set layouts

    if (DsLayoutGPassMaterial) {
        vkDestroyDescriptorSetLayout(device, DsLayoutGPassMaterial, nullptr);
        DsLayoutGPassMaterial = nullptr;
    }
    if (DsLayoutGPassSkinned) {
        vkDestroyDescriptorSetLayout(device, DsLayoutGPassSkinned, nullptr);
        DsLayoutGPassSkinned = nullptr;
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

    PlGeometrySkinned.Destroy();
}


void DeferredRenderer::CreateLightingDSLayout()
{
    // Fragment DS

    DsLayoutBuilder builder {};

    // sDepth
    builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
    // sAlbedo
    builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
    // sNormal
    builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
    // sShadowDepth
    builder.AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);

    builder.AddBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, eShaderType::Pixel);

    DsLayoutLightingFrag = builder.Build();
}

VkPipelineLayout DeferredRenderer::CreateLightingPipelineLayout()
{
    VkDescriptorSetLayout layouts[] = {
        DsLayoutLightingFrag,
        DsLayoutLightingMaterialProperties,
        gObjectManager->DsLayoutObjectBuffer,
    };

    StackArray<PushConstants, 2> push_consts = {
        PushConstants { .Size = sizeof(LightVertPushConstants), .StageFlags = VK_SHADER_STAGE_VERTEX_BIT },
    };

    VkPipelineLayout layout = Pipeline::CreateLayout(Slice(push_consts), MakeSlice(layouts, std::size(layouts)));
    Util::SetDebugLabel("Lighting Pipeline Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    PlLightingOutsideVolume.SetLayout(layout);
    PlLightingInsideVolume.SetLayout(layout);

    return layout;
}

void DeferredRenderer::CreateLightingPipeline()
{
    if (DsLayoutLightingFrag == nullptr) {
        CreateLightingDSLayout();
    }

    LightPass.Create(gRenderer->Swapchain.Extent);


    LightPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

    Target* light_target = LightPass.GetTarget(eImageFormat::RGBA16_Float);
    Assert(light_target != nullptr);
    light_target->FinalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // LightPass.AddTarget(ImageFormat::eD32_Float, Target::scFullScreen,
    //                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    //                     ImageAspectFlag::Depth);

    // Target* depth_attachment = LightPass.GetTarget(ImageFormat::eD32_Float);
    // depth_attachment->LoadOp = LoadOp::Load;
    // depth_attachment->StoreOp = StoreOp::DontCare;
    // depth_attachment->bRenderPassOnly = true;
    // depth_attachment->SetImage(GPass.GetTarget(ImageFormat::eD32_Float)->GetImage());


    LightPass.BuildRenderStage();

    DsLighting.Create(DescriptorPool, DsLayoutLightingFrag, true);

    // sDepth
    DsLighting.AddImageFromTarget(0, GPass.GetTarget(eImageFormat::eD32_Float), &gRenderer->Swapchain.DepthSampler);
    // sAlbedo
    DsLighting.AddImageFromTarget(1, GPass.GetTarget(eImageFormat::BGRA8_UNorm), &gRenderer->Swapchain.ColorSampler);
    // sNormals
    DsLighting.AddImageFromTarget(2, GPass.GetTarget(eImageFormat::RGBA16_Float), &gRenderer->Swapchain.NormalsSampler);
    // Skip 3 for the shadow target, added by DirectionalShadows
    DsLighting.AddBuffer(4, &gRenderer->ShaderUniform.GetGpuBuffer(), 0, gRenderer->ShaderUniform.Size);

    DsLighting.Build();

    Shader lighting_shader("Lighting");

    VkPipelineLayout layout = CreateLightingPipelineLayout();

    {
        Ref<ShaderProgram> vertex_shader = lighting_shader.GetProgram(eShaderType::Vertex, {});
        Ref<ShaderProgram> fragment_shader = lighting_shader.GetProgram(eShaderType::Pixel, {});

        VertexDescription vertex_info = VertexUtil::BuildDescription<eVertexType::Slim>();

        PipelineBuilder builder {};
        builder.SetLayout(layout)
            .SetName("Lighting(Point)")
            .AddBlendAttachment(
                0,
                {
                    .Enabled = true,
                    .AlphaBlend { .Ops {
                        .Src = VK_BLEND_FACTOR_ONE,
                        .Dst = VK_BLEND_FACTOR_ZERO,
                    } },
                    .ColorBlend { .Ops { .Src = VK_BLEND_FACTOR_SRC_ALPHA, .Dst = VK_BLEND_FACTOR_ONE } },
                })
            .SetOutputTargets(&LightPass.GetTargets())
            .SetShaders(vertex_shader, fragment_shader)
            .SetRenderPass(&LightPass.GetRenderPass())
            .SetVertexDescription(&vertex_info)
            .SetProperties({ .bDisableDepthTest = true, .bDisableDepthWrite = true })
            .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

        builder.SetCullMode(VK_CULL_MODE_BACK_BIT).Build(PlLightingOutsideVolume);
        builder.SetCullMode(VK_CULL_MODE_BACK_BIT).Build(PlLightingInsideVolume);
    }
    {
        SizedArray<ShaderMacro> directional_macros { ShaderMacro { "FX_LIGHT_DIRECTIONAL", "1" } };

        Ref<ShaderProgram> vertex_shader = lighting_shader.GetProgram(eShaderType::Vertex, directional_macros);
        Ref<ShaderProgram> fragment_shader = lighting_shader.GetProgram(eShaderType::Pixel, directional_macros);

        PipelineBuilder builder {};

        builder.SetLayout(layout)
            .SetName("Lighting(Directional)")
            .AddBlendAttachment(
                0,
                {
                    .Enabled = true,
                    .AlphaBlend { .Ops {
                        .Src = VK_BLEND_FACTOR_ONE,
                        .Dst = VK_BLEND_FACTOR_ZERO,
                    } },
                    .ColorBlend { .Ops { .Src = VK_BLEND_FACTOR_SRC_ALPHA, .Dst = VK_BLEND_FACTOR_ONE } },
                })
            .SetOutputTargets(&LightPass.GetTargets())
            .SetShaders(vertex_shader, fragment_shader)
            .SetRenderPass(&LightPass.GetRenderPass())
            .SetVertexDescription(nullptr)
            .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

        builder.Build(PlLightingDirectional);
    }
}

void DeferredRenderer::DestroyLightingPipeline()
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

    PlLightingDirectional.Layout = nullptr;
    PlLightingDirectional.Destroy();
}

//////////////////////////////////////////
// DeferredRenderer CompPass Functions
//////////////////////////////////////////

VkPipelineLayout DeferredRenderer::CreateCompPipelineLayout()
{
    {
        DsLayoutBuilder builder {};

        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel)
            .AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel)
            .AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);

        DsLayoutCompFrag = builder.Build();
    }


    VkDescriptorSetLayout layouts[] = {
        DsLayoutCompFrag,
    };

    StackArray<PushConstants, 1> push_consts = { PushConstants { .Size = sizeof(CompositionPushConstants),
                                                                 .StageFlags = VK_SHADER_STAGE_FRAGMENT_BIT } };

    VkPipelineLayout layout = Pipeline::CreateLayout(Slice(push_consts), MakeSlice(layouts, std::size(layouts)));

    Util::SetDebugLabel("Composition Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);
    PlComposition.SetLayout(layout);


    return layout;
}

void DeferredRenderer::CreateCompPipeline()
{
    VkPipelineLayout comp_layout = CreateCompPipelineLayout();

    CreateCompPass();


    Shader shader_composition("Composition");

    Ref<ShaderProgram> lit_vertex_shader = shader_composition.GetProgram(eShaderType::Vertex, {});
    Ref<ShaderProgram> lit_fragment_shader = shader_composition.GetProgram(eShaderType::Pixel, {});

    PipelineBuilder builder;

    builder.SetLayout(comp_layout)
        .SetName("Composition Pipeline")
        .AddBlendAttachment(0, { .Enabled = false })
        .SetOutputTargets(&CompPass.GetTargets())
        .SetShaders(lit_vertex_shader, lit_fragment_shader)
        .SetRenderPass(&CompPass.GetRenderPass())
        .SetVertexDescription(nullptr)
        .SetCullMode(VK_CULL_MODE_NONE)
        .SetWindingOrder(VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetProperties({ .bDisableDepthTest = true, .bDisableDepthWrite = true });


    builder.Build(PlComposition);


    SizedArray<ShaderMacro> unlit_macros { ShaderMacro { .pcName = "RENDER_UNLIT", .pcValue = "1" } };

    Ref<ShaderProgram> unlit_vertex_shader = shader_composition.GetProgram(eShaderType::Vertex, unlit_macros);
    Ref<ShaderProgram> unlit_fragment_shader = shader_composition.GetProgram(eShaderType::Pixel, unlit_macros);

    builder.SetShaders(unlit_vertex_shader, unlit_fragment_shader).Build(PlCompositionUnlit);
}

void DeferredRenderer::DestroyCompPipeline()
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

void DeferredRenderer::CreateCompPass()
{
    DsComposition.Create(DescriptorPool, DsLayoutCompFrag, false);

    CompPass.Create(gRenderer->Swapchain.Extent);

    CompPass.ClearValues.Insert(VkClearValue { .color = { { 0.0f, 0.3f, 0.0f, 1.0f } } });

    CompPass.MarkFinalStage();
    CompPass.BuildRenderStage();

    DsComposition.AddImageFromTarget(1, GPass.GetTarget(eImageFormat::eD32_Float), &gRenderer->Swapchain.DepthSampler);

    DsComposition.AddImageFromTarget(2, GPass.GetTarget(eImageFormat::BGRA8_UNorm), &gRenderer->Swapchain.ColorSampler);

    DsComposition.AddImageFromTarget(3, GPass.GetTarget(eImageFormat::RGBA16_Float),
                                     &gRenderer->Swapchain.NormalsSampler);

    DsComposition.AddImageFromTarget(4, LightPass.GetTarget(eImageFormat::RGBA16_Float),
                                     &gRenderer->Swapchain.LightsSampler);


    DsComposition.Build();
}

void DeferredRenderer::DoCompPass(Camera& camera)
{
    CommandBuffer& cmd = gRenderer->GetFrame()->CommandBuffer;

    CompositionPushConstants push_constants {};
    memcpy(push_constants.ViewInverse, camera.InvViewMatrix.RawData, sizeof(Mat4f));
    memcpy(push_constants.ProjInverse, camera.InvProjectionMatrix.RawData, sizeof(Mat4f));


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

} // namespace fx::renderer
