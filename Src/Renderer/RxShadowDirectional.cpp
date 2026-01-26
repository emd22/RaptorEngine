#include "RxShadowDirectional.hpp"

#include <FxEngine.hpp>
#include <FxObjectManager.hpp>
#include <Renderer/Backend/RxDsLayoutBuilder.hpp>
#include <Renderer/Backend/RxUtil.hpp>
#include <Renderer/RxDeferred.hpp>
#include <Renderer/RxPipelineBuilder.hpp>
#include <Renderer/RxRenderBackend.hpp>

FX_SET_MODULE_NAME("RxShadowDirectional")


RxShadowDirectional::RxShadowDirectional(const FxVec2u& size)
{
    RenderStage.Create(size);

    RenderStage.AddTarget(RxImageFormat::eD32_Float, size,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          RxImageAspectFlag::eDepth);

    RenderStage.BuildRenderStage();

    ShadowCamera.Update();

    FxStackArray<VkDescriptorSetLayout, 2> desc_sets = {
        gObjectManager->DsLayoutObjectBuffer,
    };

    FxStackArray<RxPushConstants, 1> push_consts = {
        RxPushConstants { .Size = sizeof(RxShadowPushConstants), .StageFlags = VK_SHADER_STAGE_VERTEX_BIT },
    };

    VkPipelineLayout pipeline_layout = RxPipeline::CreateLayout(FxSlice(push_consts), FxSlice(desc_sets));

    RxShader shader_shadow("Shadows");
    FxRef<RxShaderProgram> vertex_shader = shader_shadow.GetProgram(RxShaderType::eVertex, {});
    FxRef<RxShaderProgram> fragment_shader = shader_shadow.GetProgram(RxShaderType::eFragment, {});

    FxVertexInfo vertex_info = FxMakeVertexInfo();

    RxPipelineProperties pipeline_properties {
        .ViewportSize = size,
        .DepthCompareOp = VK_COMPARE_OP_GREATER,
    };

    RxPipelineBuilder builder {};
    builder.SetLayout(pipeline_layout)
        .SetName("Shadow Pipeline")
        .AddBlendAttachment({ .Enabled = false })
        .SetProperties(pipeline_properties)
        .SetAttachments(&RenderStage.GetTargets())
        .SetShaders(vertex_shader, fragment_shader)
        .SetRenderPass(&RenderStage.GetRenderPass())
        .SetVertexInfo(&vertex_info)
        .SetCullMode(VK_CULL_MODE_BACK_BIT)
        .SetWindingOrder(VK_FRONT_FACE_CLOCKWISE);

    builder.Build(mPipeline);

    UpdateLightDescriptors();
}

void RxShadowDirectional::Begin()
{
    RxCommandBuffer& cmd = gRenderer->GetFrame()->CommandBuffer;

    RenderStage.Begin(cmd, mPipeline);

    //VkDescriptorSet desc_sets[] = { gObjectManager->mObjectBufferDS.Set };
    /*RxDescriptorSet::BindMultiple(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline,
                                  FxSlice(desc_sets, FxSizeofArray(desc_sets)));*/
}

void RxShadowDirectional::End() { RenderStage.End(); }

void RxShadowDirectional::UpdateLightDescriptors()
{
    gRenderer->pDeferredRenderer->LightPass.AddInputTarget(3, RenderStage.GetTarget(RxImageFormat::eD32_Float, 0),
                                                           &gRenderer->Swapchain.ShadowDepthSampler);

    gRenderer->pDeferredRenderer->LightPass.BuildInputDescriptors(&gRenderer->pDeferredRenderer->DsLighting);
}
