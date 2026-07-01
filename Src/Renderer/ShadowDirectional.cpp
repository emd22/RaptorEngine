#include "ShadowDirectional.hpp"

#include <Engine.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/Backend/Util.hpp>
#include <Renderer/Backend/VertexDescription.hpp>
#include <Renderer/DeferredRenderer.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/PipelineBuilder.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/RenderBackend.hpp>
#include <Renderer/State.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("ShadowDirectional")

ShadowDirectional::ShadowDirectional(const Vec2u& size)
{
    RenderStage.Create("Shadows", size);

    RenderStage.AddTarget(eImageFormat::eD32_Float, size,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          eImageAspectFlag::Depth);

    RenderStage.BuildRenderStage();

    ShadowCamera.Update();

    // StackArray<VkDescriptorSetLayout, 2> desc_sets = {
    //     gObjectManager->DsLayoutObjectBuffer,
    // };

    // StackArray<PushConstants, 1> push_consts = {
    //     PushConstants { .Size = sizeof(ShadowPushConstants), .ShaderTypes = eShaderType::Vertex },
    // };

    // PipelineLayout pipeline_layout = PipelineLayout(Slice(push_consts), Slice(desc_sets));

    // Shader shader_shadow("Shadows");
    // VertexDescription vertex_info = VertexUtil::BuildDescription<eVertexType::Default>();

    // PipelineProperties pipeline_properties {
    //     .ViewportSize = size,
    //     .DepthCompareOp = VK_COMPARE_OP_GREATER,
    // };


    // Ref<ShaderProgram> vertex_shader = shader_shadow.GetProgram(eShaderType::Vertex, {});
    // Ref<ShaderProgram> fragment_shader = shader_shadow.GetProgram(eShaderType::Pixel, {});

    gState->BeginPipeline(ePipelineName::ShadowDirectional);
    gState->SetPushConstants(eShaderType::Vertex, sizeof(ShadowPushConstants));
    gState->UseRenderStage(RenderStage);

    gState->SetVertexType(eVertexType::Default);
    gState->SetShader(eShaderName::Shadows, {});
    gState->SetViewportSize(size);
    gState->SetDepthCompareOp(VK_COMPARE_OP_GREATER);
    gState->SetCullMode(eCullMode::Back);
    gState->SetFaceOrder(eFaceOrder::Reverse);

    gState->EndPipeline();


    // PipelineBuilder builder {};
    // builder.SetLayout(pipeline_layout)
    //     .SetName("Shadow Pipeline")
    //     .SetProperties(pipeline_properties)
    //     .SetOutputTargets(&RenderStage.GetTargets())
    //     .SetShaders(vertex_shader, fragment_shader)
    //     .SetRenderPass(&RenderStage.GetRenderPass())
    //     .SetVertexDescription(&vertex_info)
    //     .SetCullMode(VK_CULL_MODE_BACK_BIT)
    //     .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

    // builder.Build(mPipeline);

    // gState->BeginPipeline(ePipelineName::ShadowDirectional);


    // gState->EndPipeline();

    // {
    //     SizedArray<ShaderMacro> macros = { ShaderMacro { "USE_SKINNING", "1" } };

    //     vertex_shader = shader_shadow.GetProgram(eShaderType::Vertex, macros);
    //     fragment_shader = shader_shadow.GetProgram(eShaderType::Pixel, macros);

    //     vertex_info = VertexUtil::BuildDescription<eVertexType::Skinned>();
    //     builder.SetVertexDescription(&vertex_info).SetShaders(vertex_shader,
    //     fragment_shader).Build(mPipelineSkinned);
    // }

    UpdateLightDescriptors();
}

void ShadowDirectional::Begin()
{
    CommandBuffer& cmd = gRenderer->GetFrame()->CmdBuffer;

    Pipeline& pl = gPipelineCache->Request(ePipelineName::ShadowDirectional);

    RenderStage.Begin(cmd, pl);

    gObjectManager->mObjectBufferDS.BindWithOffset(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl,
                                                   gObjectManager->GetBaseOffset());
}

void ShadowDirectional::End() { RenderStage.End(); }

void ShadowDirectional::UpdateLightDescriptors()
{
    DescriptorSet& ds = gRenderer->pDeferredRenderer->DsLighting;
    ds.AddImageFromTarget(3, RenderStage.GetTarget(eImageFormat::eD32_Float), &gRenderer->Swapchain.ShadowDepthSampler);
    ds.Build();
}

} // namespace fx::renderer
