#include "DeferredRenderer.hpp"

#include "Backend/DsLayoutBuilder.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "Backend/Shader.hpp"
#include "Backend/VertexDescription.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "Globals.hpp"
#include "PipelineBuilder.hpp"
#include "PipelineCache.hpp"
#include "RenderBackend.hpp"
#include "ShaderCache.hpp"
#include "ShadowDirectional.hpp"
#include "State.hpp"

#include <Object/ObjectManager.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("DeferredRenderer")

void DeferredRenderer::Create(const Vec2u& extent)
{
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10);
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 20);
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 15);
	DescriptorPool.Create(gRenderer->GetDevice(), 16);

	CreateGPassPipeline();
	CreateLightingPipeline();
	CreateCompPipeline();
	CreateUnlitPipeline();

	CreateDescriptorSets();
}

void DeferredRenderer::Destroy()
{
	DestroyCompPipeline();
	DestroyGPassPipeline();
	DestroyLightingPipeline();
}

void DeferredRenderer::CreateForwardPass()
{
	TargetList targets {};

	Target* lp_light_attachment = LightPass.GetTarget(eImageFormat::RGBA16_Float);
	Target* lp_depth_attachment = GPass.GetTarget(eImageFormat::eD32_Float);

	Assert(lp_light_attachment != nullptr && lp_depth_attachment != nullptr);

	ForwardPass.Create("Forward", gRenderer->Swapchain.Extent);

	ForwardPass.AddTarget(eImageFormat::eD32_Float, Target::scFullScreen,
						  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						  eImageAspectFlag::Depth);
	{
		Target* depth_target = ForwardPass.GetTarget(eImageFormat::eD32_Float);
		depth_target->LoadOp = eLoadOp::Load;
		depth_target->StoreOp = eStoreOp::DontCare;
		depth_target->InitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		depth_target->UseImageFromTarget(lp_depth_attachment);
	}

	ForwardPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
						  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);
	{
		Target* light_target = ForwardPass.GetTarget(eImageFormat::RGBA16_Float);
		light_target->LoadOp = eLoadOp::Load;
		light_target->StoreOp = eStoreOp::Store;
		light_target->InitialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		light_target->FinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		light_target->UseImageFromTarget(lp_light_attachment);
	}

	ForwardPass.BuildRenderStage();
}

void DeferredRenderer::CreateGPass()
{
	GPass.Create("Geometry", gRenderer->Swapchain.Extent);

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
		builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, eShaderType::Pixel);
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
	CreateForwardPass();

	// Unlit pipeline
	gRenderState->BeginPipeline(ePipelineName::Unlit);
	gRenderState->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));
	gRenderState->SetShader(eShaderName::Unlit, {});

	gRenderState->UseRenderStage(ForwardPass);
	gRenderState->SetVertexType(eVertexType::Default);
	gRenderState->SetCullMode(eCullMode::Back);

	gRenderState->EndPipeline();

	gRenderState->AddBuffer(0, 2, &gObjectManager->mObjectGpuBuffer, 0, 100);


	// Text rendering pipeline
	gRenderState->BeginPipeline(ePipelineName::TextRendering);
	gRenderState->SetLayout(ePipelineName::Unlit);

	gRenderState->UseRenderStage(ForwardPass);
	gRenderState->SetShader(eShaderName::Text, {});
	gRenderState->SetCullMode(eCullMode::None);
	gRenderState->EndPipeline();


	// Debug Layer pipeline
	gRenderState->BeginPipeline(ePipelineName::DebugLayer);
	gRenderState->SetPushConstants(eShaderType::Vertex, sizeof(DebugLayerPushConstants));
	gRenderState->SetShader(eShaderName::Unlit, { ShaderMacro { .pcName = "IS_DEBUG_LAYER", .pcValue = "1" } });
	gRenderState->SetVertexType(eVertexType::Slim);
	gRenderState->SetRenderLines(true);
	gRenderState->SetCullMode(eCullMode::Back);

	gRenderState->UseRenderStage(ForwardPass);
	gRenderState->EndPipeline();
}


void DeferredRenderer::CreateGPassPipeline()
{
	CreateGPassPipelineLayout();

	CreateGPass();
	{
		gRenderState->BeginPipeline(ePipelineName::Geometry);
		gRenderState->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

		gRenderState->UseRenderStage(GPass);
		gRenderState->SetShader(eShaderName::Geometry, {});
		gRenderState->SetVertexType(eVertexType::Default);
		gRenderState->SetCullMode(eCullMode::Back);

		gRenderState->EndPipeline();

		Ref<Shader> geometry_shader = gShaderCache->Request(eShaderName::Geometry);
	}


	Pipeline& geometry_pl = gPipelineCache->Request(ePipelineName::Geometry);
	geometry_pl.VertexShader->PrintReflection();


	// Normal mapped pipeline
	gRenderState->BeginPipeline(ePipelineName::GeometryNormalMaps);
	// Use previous layout
	gRenderState->SetLayout(ePipelineName::Geometry);

	gRenderState->UseRenderStage(GPass);
	gRenderState->SetShader(eShaderName::Geometry, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" } });
	gRenderState->SetVertexType(eVertexType::Default);
	gRenderState->SetCullMode(eCullMode::Back);

	gRenderState->EndPipeline();

	CreateGPassSkinnedPipelineLayout();

	// Skinned + Normal mapped pipeline
	gRenderState->BeginPipeline(ePipelineName::GeometrySkinned);
	gRenderState->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

	gRenderState->UseRenderStage(GPass);
	gRenderState->SetVertexType(eVertexType::Skinned);
	gRenderState->SetShader(eShaderName::Geometry, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" },
													 ShaderMacro { .pcName = "USE_SKINNING", .pcValue = "1" } });
	gRenderState->SetCullMode(eCullMode::Back);
	gRenderState->EndPipeline();


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
		LightPass.Create("Lighting", gRenderer->Swapchain.Extent);


		LightPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
							VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

		Target* light_target = LightPass.GetTarget(eImageFormat::RGBA16_Float);
		Assert(light_target != nullptr);
		light_target->FinalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		LightPass.BuildRenderStage();
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
	gRenderState->BeginPipeline(ePipelineName::LightingInsideVolume);
	gRenderState->SetPushConstants(eShaderType::Vertex, sizeof(LightVertPushConstants));

	gRenderState->UseRenderStage(LightPass);
	gRenderState->SetTargetBlend(0, lighting_blend);
	gRenderState->SetShader(eShaderName::Lighting, {});
	gRenderState->SetVertexType(eVertexType::Slim);

	gRenderState->SetDepthTest(false);
	gRenderState->SetDepthWrite(false);

	gRenderState->SetFaceOrder(eFaceOrder::Reverse);
	gRenderState->SetCullMode(eCullMode::Back);

	gRenderState->EndPipeline();

	// Point light pipeline (outside)

	gRenderState->BeginPipeline(ePipelineName::LightingOutsideVolume);
	gRenderState->SetLayout(ePipelineName::LightingInsideVolume);

	gRenderState->UseRenderStage(LightPass);
	gRenderState->SetTargetBlend(0, lighting_blend);
	gRenderState->SetShader(eShaderName::Lighting, {});
	gRenderState->SetVertexType(eVertexType::Slim);

	gRenderState->SetDepthTest(false);
	gRenderState->SetDepthWrite(false);

	gRenderState->SetFaceOrder(eFaceOrder::Reverse);
	gRenderState->SetCullMode(eCullMode::Back);

	gRenderState->EndPipeline();


	// Directional lighting pipeline
	gRenderState->BeginPipeline(ePipelineName::LightingDirectional);
	gRenderState->SetLayout(ePipelineName::LightingInsideVolume);

	gRenderState->UseRenderStage(LightPass);
	gRenderState->SetTargetBlend(0, lighting_blend);
	gRenderState->SetShader(eShaderName::Lighting,
							{ ShaderMacro { .pcName = "FX_LIGHT_DIRECTIONAL", .pcValue = "1" } });
	gRenderState->SetVertexType(eVertexType::Slim);

	gRenderState->SetDepthTest(false);
	gRenderState->SetDepthWrite(false);

	gRenderState->SetFaceOrder(eFaceOrder::Reverse);
	gRenderState->SetCullMode(eCullMode::None);

	// Since the directional light is a triangle built from the screen coordinates, we won't be passing in vertices.
	gRenderState->SetFlags(eStateFlags::NoVertices);

	gRenderState->EndPipeline();
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

	gRenderState->BeginPipeline(ePipelineName::Composition);
	gRenderState->SetPushConstants(eShaderType::Pixel, sizeof(CompositionPushConstants));

	gRenderState->UseRenderStage(CompPass);
	gRenderState->SetShader(eShaderName::Composition, {});
	gRenderState->SetFlags(eStateFlags::NoVertices);
	gRenderState->SetCullMode(eCullMode::None);
	gRenderState->SetFaceOrder(eFaceOrder::Default);
	gRenderState->SetDepthTest(false);
	gRenderState->SetDepthWrite(false);
	gRenderState->EndPipeline();
}

void DeferredRenderer::DestroyCompPipeline()
{
	VkDevice device = gRenderer->GetDevice()->Device;

	// Destroy descriptor set layouts
	if (DsLayoutCompFrag) {
		vkDestroyDescriptorSetLayout(device, DsLayoutCompFrag, nullptr);
		DsLayoutCompFrag = nullptr;
	}
}

void DeferredRenderer::CreateDescriptorSets()
{
	DsComposition.Destroy();
	DsComposition.Create(DescriptorPool, DsLayoutCompFrag, false);
	DsComposition.AddImageFromTarget(1, GPass.GetTarget(eImageFormat::eD32_Float),
									 gSamplerCache->Request(SamplerProps {
										 eSamplerFilter::Nearest,
										 eSamplerFilter::Nearest,
										 eSamplerFilter::Nearest,
									 }));
	DsComposition.AddImageFromTarget(2, GPass.GetTarget(eImageFormat::BGRA8_UNorm),
									 gSamplerCache->Request(SamplerProps {}));
	DsComposition.AddImageFromTarget(3, GPass.GetTarget(eImageFormat::RGBA16_Float),
									 gSamplerCache->Request(SamplerProps {}));
	DsComposition.AddImageFromTarget(4, LightPass.GetTarget(eImageFormat::RGBA16_Float),
									 gSamplerCache->Request(SamplerProps {}));
	DsComposition.Build();


	DsLighting.Destroy();
	DsLighting.Create(DescriptorPool, DsLayoutLightingFrag, true);
	// sDepth
	DsLighting.AddImageFromTarget(0, GPass.GetTarget(eImageFormat::eD32_Float), &gRenderer->Swapchain.DepthSampler);
	// sAlbedo
	DsLighting.AddImageFromTarget(1, GPass.GetTarget(eImageFormat::BGRA8_UNorm), &gRenderer->Swapchain.ColorSampler);

	// sNormals
	DsLighting.AddImageFromTarget(2, GPass.GetTarget(eImageFormat::RGBA16_Float), &gRenderer->Swapchain.NormalsSampler);

	if (gShadowRenderer != nullptr && gShadowRenderer->RenderStage.IsBuilt()) {
		DsLighting.AddImageFromTarget(3, gShadowRenderer->RenderStage.GetTarget(eImageFormat::eD32_Float),
									  &gRenderer->Swapchain.ShadowDepthSampler);
	}

	// Skip 3 for the shadow target, added by DirectionalShadows
	DsLighting.AddBuffer(4, &gRenderer->LightBuffer.GetGpuBuffer(), 0, gRenderer->LightBuffer.PageSize);

	DsLighting.Build();
}

void DeferredRenderer::CreateCompPass()
{
	CompPass.Create("Compose", gRenderer->Swapchain.Extent);

	CompPass.MarkFinalStage();
	CompPass.BuildRenderStage();
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
