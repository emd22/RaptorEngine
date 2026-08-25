#include "ShadowDirectional.hpp"

#include <Engine.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/Backend/Util.hpp>
#include <Renderer/Backend/VertexDescription.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/PSOBuild.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/TiledForwardRenderer.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("ShadowDirectional")

ShadowDirectional::ShadowDirectional(const Vec2u& size)
{
	RenderStage.Create("Shadows", size);

	RenderStage.AddTarget(eImageFormat::D32_Float, size,
						  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
						  eImageAspectFlag::Depth);

	RenderStage.BuildRenderStage();

	ShadowCamera.Update();

	{
		gPSOBuild->BeginPipeline(ePipelineName::ShadowDirectional);
		gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(ShadowPushConstants));
		gPSOBuild->UseRenderStage(RenderStage);

		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetShader(eShaderName::Shadows, {});
		gPSOBuild->SetViewportSize(size);
		gPSOBuild->SetDepthCompareOp(VK_COMPARE_OP_GREATER);
		gPSOBuild->SetCullMode(eCullMode::Back);
		gPSOBuild->SetFaceOrder(eFaceOrder::Reverse);

		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());

		gPSOBuild->AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
							 gMaterialManager->MaterialPropertiesBuffer.Size);

		gPSOBuild->EndPipeline();
	}

	UpdateLightDescriptors();
}

void ShadowDirectional::Begin()
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	RenderStage.Begin(cmd);
	gPipelineCache->AddBufferOffset(0, gObjectManager->GetBaseOffset());
	gPipelineCache->AddBufferOffset(0, 0);
	gPipelineCache->Bind(ePipelineName::ShadowDirectional, cmd);
}

void ShadowDirectional::End() { RenderStage.End(); }

void ShadowDirectional::UpdateLightDescriptors()
{
	// DescriptorSet& ds = gRenderer->pDeferredRenderer->DsLighting;
	// ds.AddImageFromTarget(3, RenderStage.GetTarget(eImageFormat::D32_Float),
	// &gRenderer->Swapchain.ShadowDepthSampler); ds.Build();
}

} // namespace fx::renderer
