#include "RxShadowDirectional.hpp"

#include <Engine.hpp>
#include <ObjectManager.hpp>
#include <Renderer/Backend/RxDsLayoutBuilder.hpp>
#include <Renderer/Backend/RxUtil.hpp>
#include <Renderer/Backend/RxVertexDescription.hpp>
#include <Renderer/RxDeferred.hpp>
#include <Renderer/RxGlobals.hpp>
#include <Renderer/RxPipelineBuilder.hpp>
#include <Renderer/RxRenderBackend.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("RxShadowDirectional")

RxShadowDirectional::RxShadowDirectional(const Vec2u& size)
{
    RenderStage.Create(size);

    RenderStage.AddTarget(RxImageFormat::eD32_Float, size,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          RxImageAspectFlag::eDepth);

    RenderStage.BuildRenderStage();

    ShadowCamera.Update();

    StackArray<VkDescriptorSetLayout, 2> desc_sets = {
        gObjectManager->DsLayoutObjectBuffer,
    };

    StackArray<RxPushConstants, 1> push_consts = {
        RxPushConstants { .Size = sizeof(RxShadowPushConstants), .StageFlags = VK_SHADER_STAGE_VERTEX_BIT },
    };

    VkPipelineLayout pipeline_layout = RxPipeline::CreateLayout(Slice(push_consts), Slice(desc_sets));

    RxShader shader_shadow("Shadows");
    RxVertexDescription vertex_info = RxVertexUtil::BuildDescription<RxVertexType::eDefault>();

    RxPipelineProperties pipeline_properties {
        .ViewportSize = size,
        .DepthCompareOp = VK_COMPARE_OP_GREATER,
    };


    Ref<RxShaderProgram> vertex_shader = shader_shadow.GetProgram(RxShaderType::eVertex, {});
    Ref<RxShaderProgram> fragment_shader = shader_shadow.GetProgram(RxShaderType::eFragment, {});

    RxPipelineBuilder builder {};
    builder.SetLayout(pipeline_layout)
        .SetName("Shadow Pipeline")
        .AddBlendAttachment({ .Enabled = false })
        .SetProperties(pipeline_properties)
        .SetAttachments(&RenderStage.GetTargets())
        .SetShaders(vertex_shader, fragment_shader)
        .SetRenderPass(&RenderStage.GetRenderPass())
        .SetVertexDescription(&vertex_info)
        .SetCullMode(VK_CULL_MODE_BACK_BIT)
        .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

    builder.Build(mPipeline);

    {
        SizedArray<ShaderMacro> macros = { ShaderMacro { "USE_SKINNING", "1" } };

        vertex_shader = shader_shadow.GetProgram(RxShaderType::eVertex, macros);
        fragment_shader = shader_shadow.GetProgram(RxShaderType::eFragment, macros);

        vertex_info = RxVertexUtil::BuildDescription<RxVertexType::eSkinned>();
        builder.SetVertexDescription(&vertex_info).SetShaders(vertex_shader, fragment_shader).Build(mPipelineSkinned);
    }

    UpdateLightDescriptors();
}

void RxShadowDirectional::Begin()
{
    RxCommandBuffer& cmd = gRenderer->GetFrame()->CommandBuffer;

    RenderStage.Begin(cmd, mPipeline);

    gObjectManager->mObjectBufferDS.BindWithOffset(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                   gShadowRenderer->GetPipeline(), gObjectManager->GetBaseOffset());

    // VkDescriptorSet desc_sets[] = { gObjectManager->mObjectBufferDS.Set };
    /*RxDescriptorSet::BindMultiple(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline,
                                  Slice(desc_sets, SizeofArray(desc_sets)));*/
}

void RxShadowDirectional::End() { RenderStage.End(); }

void RxShadowDirectional::UpdateLightDescriptors()
{
    RxDescriptorSet& ds = gRenderer->pDeferredRenderer->DsLighting;
    ds.AddImageFromTarget(3, RenderStage.GetTarget(RxImageFormat::eD32_Float),
                          &gRenderer->Swapchain.ShadowDepthSampler);
    ds.Build();
}

} // namespace fx::renderer
