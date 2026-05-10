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

    return nullptr;
}


void DeferredRenderer::CreateUnlitPipeline()
{
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

    // Text rendering pipeline
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
}


void DeferredRenderer::CreateGPassPipeline()
{
    CreateGPassPipelineLayout();

    CreateGPass();

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

    gPipelineCache->Request(ePipelineName::Geometry).Destroy();
    gPipelineCache->Request(ePipelineName::GeometryNormalMaps).Destroy();
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

void DeferredRenderer::CreateLightingPipeline()
{
    if (DsLayoutLightingFrag == nullptr) {
        CreateLightingDSLayout();
    }

    {
        LightPass.Create(gRenderer->Swapchain.Extent);


        LightPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

        Target* light_target = LightPass.GetTarget(eImageFormat::RGBA16_Float);
        Assert(light_target != nullptr);
        light_target->FinalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        LightPass.BuildRenderStage();
    }
    {
        DsLighting.Create(DescriptorPool, DsLayoutLightingFrag, true);

        // sDepth
        DsLighting.AddImageFromTarget(0, GPass.GetTarget(eImageFormat::eD32_Float), &gRenderer->Swapchain.DepthSampler);
        // sAlbedo
        DsLighting.AddImageFromTarget(1, GPass.GetTarget(eImageFormat::BGRA8_UNorm),
                                      &gRenderer->Swapchain.ColorSampler);
        // sNormals
        DsLighting.AddImageFromTarget(2, GPass.GetTarget(eImageFormat::RGBA16_Float),
                                      &gRenderer->Swapchain.NormalsSampler);
        // Skip 3 for the shadow target, added by DirectionalShadows
        DsLighting.AddBuffer(4, &gRenderer->LightBuffer.GetGpuBuffer(), 0, gRenderer->LightBuffer.PageSize);

        DsLighting.Build();
    }

    Shader lighting_shader("Lighting");

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
    if (DsLayoutLightingFrag != nullptr) {
        vkDestroyDescriptorSetLayout(device, DsLayoutLightingFrag, nullptr);
        DsLayoutLightingFrag = nullptr;
    }

    if (DsLayoutLightingMaterialProperties != nullptr) {
        vkDestroyDescriptorSetLayout(device, DsLayoutLightingMaterialProperties, nullptr);
        DsLayoutLightingMaterialProperties = nullptr;
    }
}

//////////////////////////////////////////
// DeferredRenderer CompPass Functions
//////////////////////////////////////////

void DeferredRenderer::CreateCompPipeline()
{
    {
        DsLayoutBuilder builder {};

        builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel)
            .AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel)
            .AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel)
            .AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);

        DsLayoutCompFrag = builder.Build();
    }

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

    DsComposition.Bind(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composition_pipeline);

    // Use single triangle instead of two triangles as it removes the overlapping quads the gpu
    // renders between triangles. Source: https://wallisc.github.io/rendering/2021/04/18/Fullscreen-Pass.html
    vkCmdDraw(cmd.Get(), 3, 1, 0, 0);
}

} // namespace fx::renderer
