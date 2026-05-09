#include "DeferredRenderer.hpp"

#include "Backend/DsLayoutBuilder.hpp"
#include "Backend/Shader.hpp"
#include "Backend/VertexDescription.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "Globals.hpp"
#include "PipelineBuilder.hpp"
#include "PipelineCache.hpp"
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

    // StackArray<VkDescriptorSetLayout, 3> ds_layouts = {
    //     DsLayoutGPassMaterial,
    //     DsLayoutLightingMaterialProperties,
    //     gObjectManager->DsLayoutObjectBuffer,
    // };

    // StackArray<PushConstants, 1> push_consts = {
    //     PushConstants {
    //         .Size = sizeof(DrawPushConstants),
    //         .ShaderTypes = eShaderType::Vertex | eShaderType::Pixel,
    //     },
    // };

    // VkPipelineLayout layout = Pipeline::CreateLayout(Slice(push_consts), Slice(ds_layouts));

    // Util::SetDebugLabel("Geometry PL L", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return nullptr;
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


    // StackArray<VkDescriptorSetLayout, 3> ds_layouts = {
    //     DsLayoutGPassSkinned,
    //     DsLayoutLightingMaterialProperties,
    //     gObjectManager->DsLayoutObjectBuffer,
    // };

    // StackArray<PushConstants, 1> push_consts = {
    //     PushConstants {
    //         .Size = sizeof(DrawPushConstants),
    //         .ShaderTypes = eShaderType::Vertex | eShaderType::Pixel,
    //     },
    // };

    // VkPipelineLayout layout = Pipeline::CreateLayout(Slice(push_consts), Slice(ds_layouts));

    // Util::SetDebugLabel("Geometry(Skinned) PL L", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return nullptr;
}

VkPipelineLayout DeferredRenderer::CreateUnlitPipelineLayout()
{
    // StackArray<VkDescriptorSetLayout, 3> ds_layouts = {
    //     DsLayoutGPassMaterial,
    //     DsLayoutLightingMaterialProperties,
    //     gObjectManager->DsLayoutObjectBuffer,
    // };

    // StackArray<PushConstants, 1> push_consts = {
    //     PushConstants {
    //         .Size = sizeof(DrawPushConstants),
    //         .ShaderTypes = eShaderType::Vertex | eShaderType::Pixel,
    //     },
    // };

    // VkPipelineLayout layout = Pipeline::CreateLayout(Slice(push_consts), Slice(ds_layouts));

    // Util::SetDebugLabel("Unlit PL L", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    return nullptr;
}

VkPipelineLayout DeferredRenderer::CreateDebugLayerPipelineLayout()
{
    // StackArray<VkDescriptorSetLayout, 1> ds_layouts = {};

    // StackArray<PushConstants, 1> push_consts = {
    //     PushConstants {
    //         .Size = sizeof(DebugLayerPushConstants),
    //         .ShaderTypes = eShaderType::Vertex,
    //     },
    // };

    // VkPipelineLayout layout = Pipeline::CreateLayout(Slice(push_consts), Slice(ds_layouts));

    // Util::SetDebugLabel("Debug Layer PL L", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

    // return layout;
    return nullptr;
}


void DeferredRenderer::CreateUnlitPipeline()
{
    CreateUnlitPipelineLayout();

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

    // builder.SetLayout(layout)
    //     .SetName("Unlit Pipeline")
    //     .SetOutputTargets(&targets)
    //     .SetShaders(vertex_shader, pixel_shader)
    //     .SetRenderPass(&RpForward)
    //     .SetVertexDescription(&vertex_info)
    //     .SetCullMode(VK_CULL_MODE_BACK_BIT)
    //     .SetWindingOrder(VK_FRONT_FACE_COUNTER_CLOCKWISE);
    // builder.Build(PlUnlit);

    // Unlit pipeline
    gState->BeginPipeline(ePipelineName::Unlit);
    gState->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));
    // Descriptors
    gState->AddDescriptor(DsLayoutGPassMaterial);
    gState->AddDescriptor(DsLayoutLightingMaterialProperties);
    gState->AddDescriptor(gObjectManager->DsLayoutObjectBuffer);

    gState->SetShader(eShaderName::Unlit, {});
    gState->SetOutputTargets(&targets);
    gState->SetRenderPass(&RpForward);
    gState->SetVertexType(eVertexType::Default);
    gState->SetCullMode(eCullMode::Back);

    gState->EndPipeline();

    // {
    //     Ref<Shader> shader_text = gShaderCache->Request(eShaderName::Text);
    //     vertex_shader = shader_text->GetProgram(eShaderType::Vertex, {});
    //     pixel_shader = shader_text->GetProgram(eShaderType::Pixel, {});

    //     builder.SetLayout(layout)
    //         .SetName("Text Pipeline")
    //         .SetOutputTargets(&targets)
    //         .SetShaders(vertex_shader, pixel_shader)
    //         .SetRenderPass(&RpForward)
    //         .SetVertexDescription(&vertex_info)
    //         .SetCullMode(VK_CULL_MODE_BACK_BIT)
    //         .SetWindingOrder(VK_FRONT_FACE_COUNTER_CLOCKWISE);
    //     builder.Build(PlText);
    // }


    gState->BeginPipeline(ePipelineName::TextRendering);
    gState->SetLayout(ePipelineName::Unlit);
    gState->SetOutputTargets(&targets);
    gState->SetRenderPass(&RpForward);
    gState->SetShader(eShaderName::Text, {});
    gState->SetCullMode(eCullMode::None);
    gState->EndPipeline();


    // Debug Layer pipeline
    gState->BeginPipeline(ePipelineName::DebugLayer);
    gState->SetPushConstants(eShaderType::Vertex, sizeof(DebugLayerPushConstants));
    gState->SetShader(eShaderName::Unlit, { ShaderMacro { .pcName = "IS_DEBUG_LAYER", .pcValue = "1" } });
    gState->SetVertexType(eVertexType::Slim);
    gState->SetRenderLines(true);
    gState->SetCullMode(eCullMode::Back);
    gState->SetRenderPass(&RpForward);
    gState->SetOutputTargets(&targets);
    gState->EndPipeline();

    // Create debug layer pipeline

    // VkPipelineLayout debug_layout = CreateDebugLayerPipelineLayout();

    // vertex_shader = shader_unlit->GetProgram(eShaderType::Vertex,
    //                                          { ShaderMacro { .pcName = "IS_DEBUG_LAYER", .pcValue = "1" } });

    // pixel_shader = shader_unlit->GetProgram(eShaderType::Pixel,
    //                                         { ShaderMacro { .pcName = "IS_DEBUG_LAYER", .pcValue = "1" } });


    // VertexDescription debug_vertex_description = VertexUtil::BuildDescription<eVertexType::Slim>();

    // builder.SetLayout(debug_layout)
    //     .SetName("Debug Layer Pipeline")
    //     .SetShaders(vertex_shader, pixel_shader)
    //     .SetWindingOrder(VK_FRONT_FACE_COUNTER_CLOCKWISE)
    //     .SetVertexDescription(&debug_vertex_description)
    //     .SetProperties({ .bRenderLines = true })
    //     .Build(PlDebugLayer);
}


void DeferredRenderer::CreateGPassPipeline()
{
    CreateGPassPipelineLayout();

    CreateGPass();

    {
        // gState->BeginPipeline(ePipelineName::Geometry);
        // gState->SetOutputTargets(GPass.GetTargets());
        // gState->SetShader(eShaderName::Geometry, {});
        // gState->SetCullMode(eCullMode::Back);
        // gState->SetRenderPass(&GPass.GetRenderPass());
        // gState->EndPipeline();
    }

    // Ref<Shader> shader_geometry = gShaderCache->Request(eShaderName::Geometry);
    // Ref<ShaderProgram> vertex_shader = shader_geometry->GetProgram(eShaderType::Vertex, {});
    // Ref<ShaderProgram> fragment_shader = shader_geometry->GetProgram(eShaderType::Pixel, {});

    // VertexDescription vertex_info = VertexUtil::BuildDescription<eVertexType::Default>();

    // PipelineBuilder builder;

    // builder.SetLayout(gpass_layout)
    //     .SetName("Geometry Pipeline")
    //     .SetOutputTargets(&GPass.GetTargets())
    //     .SetShaders(vertex_shader, fragment_shader)
    //     .SetRenderPass(&GPass.GetRenderPass())
    //     .SetVertexDescription(&vertex_info)
    //     .SetCullMode(VK_CULL_MODE_BACK_BIT)
    //     .SetWindingOrder(VK_FRONT_FACE_COUNTER_CLOCKWISE);

    gState->BeginPipeline(ePipelineName::Geometry);
    // Descriptors
    gState->AddDescriptor(DsLayoutGPassMaterial);
    gState->AddDescriptor(DsLayoutLightingMaterialProperties);
    gState->AddDescriptor(gObjectManager->DsLayoutObjectBuffer);
    gState->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

    gState->UseRenderStage(GPass);
    gState->SetShader(eShaderName::Geometry, {});
    gState->SetVertexType(eVertexType::Default);
    gState->SetCullMode(eCullMode::Back);

    gState->EndPipeline();


    // Normal mapped pipeline
    gState->BeginPipeline(ePipelineName::GeometryNormalMaps);
    // Use previous layout
    gState->SetLayout(ePipelineName::Geometry);

    gState->UseRenderStage(GPass);
    gState->SetShader(eShaderName::Geometry, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" } });
    gState->SetVertexType(eVertexType::Default);
    gState->SetCullMode(eCullMode::Back);

    gState->EndPipeline();

    CreateGPassSkinnedPipelineLayout();

    // Skinned + Normal mapped pipeline
    gState->BeginPipeline(ePipelineName::GeometrySkinned);
    gState->AddDescriptor(DsLayoutGPassSkinned);
    gState->AddDescriptor(DsLayoutLightingMaterialProperties);
    gState->AddDescriptor(gObjectManager->DsLayoutObjectBuffer);
    gState->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

    gState->UseRenderStage(GPass);
    gState->SetVertexType(eVertexType::Skinned);
    gState->SetShader(eShaderName::Geometry, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" },
                                               ShaderMacro { .pcName = "USE_SKINNING", .pcValue = "1" } });
    gState->SetCullMode(eCullMode::Back);
    gState->EndPipeline();

    // {
    //     vertex_info = VertexUtil::BuildDescription<eVertexType::Skinned>();

    //     SizedArray<ShaderMacro> macros = { ShaderMacro { "USE_NORMAL_MAPS", "1" },
    //                                        ShaderMacro { "USE_SKINNING", "1" } };

    //     Ref<ShaderProgram> nm_vertex_shader = shader_geometry->GetProgram(eShaderType::Vertex, macros);
    //     Ref<ShaderProgram> nm_fragment_shader = shader_geometry->GetProgram(eShaderType::Pixel, macros);


    //     builder.SetLayout(skinned_layout)
    //         .SetPolygonMode(VK_POLYGON_MODE_FILL)
    //         .SetVertexDescription(&vertex_info)
    //         .SetShaders(nm_vertex_shader, nm_fragment_shader)
    //         .Build(PlGeometrySkinned);
    // }

    pGeometryPipeline = &gPipelineCache->Request(ePipelineName::Geometry);
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

    // PlGeometry.Destroy();
    gPipelineCache->Request(ePipelineName::Geometry).Destroy();
    gPipelineCache->Request(ePipelineName::GeometryNormalMaps).Destroy();

    // PlGeometryWithNormalMaps.Layout = nullptr;
    // PlGeometryWithNormalMaps.Destroy();

    // PlGeometrySkinned.Destroy();
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

// PipelineLayout DeferredRenderer::CreateLightingPipelineLayout()
// {
// VkDescriptorSetLayout layouts[] = {
//     DsLayoutLightingFrag,
//     DsLayoutLightingMaterialProperties,
//     gObjectManager->DsLayoutObjectBuffer,
// };

// StackArray<PushConstants, 2> push_consts = {
//     PushConstants { .Size = sizeof(LightVertPushConstants), .ShaderTypes = eShaderType::Vertex },
// };

// PipelineLayout layout = PipelineLayout(Slice(push_consts), MakeSlice(layouts, std::size(layouts)));
// Util::SetDebugLabel("Lighting Pipeline Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);

// PlLightingOutsideVolume.SetLayout(layout);
// PlLightingInsideVolume.SetLayout(layout);

// return layout;
// }

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

    LightPass.BuildRenderStage();

    DsLighting.Create(DescriptorPool, DsLayoutLightingFrag, true);

    // sDepth
    DsLighting.AddImageFromTarget(0, GPass.GetTarget(eImageFormat::eD32_Float), &gRenderer->Swapchain.DepthSampler);
    // sAlbedo
    DsLighting.AddImageFromTarget(1, GPass.GetTarget(eImageFormat::BGRA8_UNorm), &gRenderer->Swapchain.ColorSampler);
    // sNormals
    DsLighting.AddImageFromTarget(2, GPass.GetTarget(eImageFormat::RGBA16_Float), &gRenderer->Swapchain.NormalsSampler);
    // Skip 3 for the shadow target, added by DirectionalShadows
    DsLighting.AddBuffer(4, &gRenderer->LightBuffer.GetGpuBuffer(), 0, gRenderer->LightBuffer.PageSize);

    DsLighting.Build();

    Shader lighting_shader("Lighting");

    // CreateLightingPipelineLayout();


    BlendAttachment lighting_blend = BlendAttachment {
        .Enabled = true,
        .AlphaBlend { .Ops {
            .Src = VK_BLEND_FACTOR_ONE,
            .Dst = VK_BLEND_FACTOR_ZERO,
        } },
        .ColorBlend { .Ops { .Src = VK_BLEND_FACTOR_SRC_ALPHA, .Dst = VK_BLEND_FACTOR_ONE } },
    };

    // Point light pipeline (inside)
    gState->BeginPipeline(ePipelineName::LightingInsideVolume);
    // Layout info
    gState->AddDescriptor(DsLayoutLightingFrag);
    gState->AddDescriptor(DsLayoutLightingMaterialProperties);
    gState->AddDescriptor(gObjectManager->DsLayoutObjectBuffer);
    gState->SetPushConstants(eShaderType::Vertex, sizeof(LightVertPushConstants));


    gState->UseRenderStage(LightPass);
    gState->SetTargetBlend(0, lighting_blend);
    gState->SetShader(eShaderName::Lighting, {});
    gState->SetVertexType(eVertexType::Slim);

    gState->SetDepthTest(false);
    gState->SetDepthWrite(false);

    gState->SetFaceOrder(eFaceOrder::Reverse);
    gState->SetCullMode(eCullMode::Back);

    gState->EndPipeline();

    // Point light pipeline (outside)

    gState->BeginPipeline(ePipelineName::LightingOutsideVolume);
    gState->SetLayout(ePipelineName::LightingInsideVolume);

    gState->UseRenderStage(LightPass);
    gState->SetTargetBlend(0, lighting_blend);
    gState->SetShader(eShaderName::Lighting, {});
    gState->SetVertexType(eVertexType::Slim);

    gState->SetDepthTest(false);
    gState->SetDepthWrite(false);

    gState->SetFaceOrder(eFaceOrder::Reverse);
    gState->SetCullMode(eCullMode::Back);

    gState->EndPipeline();


    // Directional lighting pipeline
    gState->BeginPipeline(ePipelineName::LightingDirectional);
    gState->SetLayout(ePipelineName::LightingInsideVolume);

    gState->UseRenderStage(LightPass);
    gState->SetTargetBlend(0, lighting_blend);
    gState->SetShader(eShaderName::Lighting, { ShaderMacro { .pcName = "FX_LIGHT_DIRECTIONAL", .pcValue = "1" } });
    gState->SetVertexType(eVertexType::Slim);

    gState->SetDepthTest(false);
    gState->SetDepthWrite(false);

    gState->SetFaceOrder(eFaceOrder::Reverse);
    gState->SetCullMode(eCullMode::None);

    // Since the directional light is a triangle built from the screen coordinates, we won't be passing in vertices.
    gState->SetFlags(eStateFlags::NoVertices);

    gState->EndPipeline();
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

    // PlLightingOutsideVolume.Destroy();

    // PlLightingInsideVolume.Layout = nullptr;
    // PlLightingInsideVolume.Destroy();

    // PlLightingDirectional.Layout = nullptr;
    // PlLightingDirectional.Destroy();
}

//////////////////////////////////////////
// DeferredRenderer CompPass Functions
//////////////////////////////////////////

PipelineLayout DeferredRenderer::CreateCompPipelineLayout()
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
                                                                 .ShaderTypes = eShaderType::Pixel } };

    PipelineLayout layout = PipelineLayout(Slice(push_consts), MakeSlice(layouts, std::size(layouts)));

    // Util::SetDebugLabel("Composition Layout", VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout);
    // PlComposition.SetLayout(layout);


    return layout;
}

void DeferredRenderer::CreateCompPipeline()
{
    PipelineLayout layout = CreateCompPipelineLayout();

    CreateCompPass();

    gState->BeginPipeline(ePipelineName::Composition);
    gState->AddDescriptor(DsLayoutCompFrag);
    gState->SetPushConstants(eShaderType::Pixel, sizeof(CompositionPushConstants));

    gState->UseRenderStage(CompPass);
    gState->SetShader(eShaderName::Composition, {});
    gState->SetFlags(eStateFlags::NoVertices);
    gState->SetCullMode(eCullMode::None);
    gState->SetFaceOrder(eFaceOrder::Default);
    gState->SetDepthTest(false);
    gState->SetDepthWrite(false);
    gState->EndPipeline();
}

void DeferredRenderer::DestroyCompPipeline()
{
    VkDevice device = gRenderer->GetDevice()->Device;

    // Destroy descriptor set layouts
    if (DsLayoutCompFrag) {
        vkDestroyDescriptorSetLayout(device, DsLayoutCompFrag, nullptr);
        DsLayoutCompFrag = nullptr;
    }

    // PlComposition.Destroy();
    // PlCompositionUnlit.Layout = nullptr;
    // PlCompositionUnlit.Destroy();
}

void DeferredRenderer::CreateCompPass()
{
    DsComposition.Create(DescriptorPool, DsLayoutCompFrag, false);

    CompPass.Create(gRenderer->Swapchain.Extent);

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
    CommandBuffer& cmd = gRenderer->GetFrame()->CmdBuffer;

    CompositionPushConstants push_constants {};
    memcpy(push_constants.ViewInverse, camera.InvViewMatrix.RawData, sizeof(Mat4f));
    memcpy(push_constants.ProjInverse, camera.InvProjectionMatrix.RawData, sizeof(Mat4f));

    Pipeline& composition_pipeline = gPipelineCache->Request(ePipelineName::Composition);

    gRenderer->SubmitPushConstants(cmd, composition_pipeline, eShaderType::Pixel, push_constants);

    // vkCmdPushConstants(cmd.Get(), PlComposition.Layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
    //                    &push_constants);

    // CompPass.Begin(cmd, composition_pipeline);

    DsComposition.Bind(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composition_pipeline);

    // Use single triangle instead of two triangles as it removes the overlapping quads the gpu
    // renders between triangles. Source: https://wallisc.github.io/rendering/2021/04/18/Fullscreen-Pass.html
    vkCmdDraw(cmd.Get(), 3, 1, 0, 0);
}

} // namespace fx::renderer
