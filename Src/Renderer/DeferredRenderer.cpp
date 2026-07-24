#include "DeferredRenderer.hpp"

#include "Backend/DescriptorCache.hpp"
#include "Backend/DsLayoutBuilder.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "Backend/Shader.hpp"
#include "Backend/VertexDescription.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "Globals.hpp"
#include "PSOBuild.hpp"
#include "PipelineCache.hpp"
#include "RenderBackend.hpp"
#include "ShaderCache.hpp"
#include "ShadowDirectional.hpp"

#include <Asset/AssetManager.hpp>
#include <Material/MaterialManager.hpp>
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
	Target* lp_depth_attachment = GPass.GetTarget(eImageFormat::D32_Float);

	Assert(lp_light_attachment != nullptr && lp_depth_attachment != nullptr);

	ForwardPass.Create("Forward", gRenderer->Swapchain.Extent);

	ForwardPass.AddTarget(eImageFormat::D32_Float, Target::scFullScreen,
						  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						  eImageAspectFlag::Depth);
	{
		Target* depth_target = ForwardPass.GetTarget(eImageFormat::D32_Float);
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
	GPass.AddTarget(eImageFormat::D32_Float, Target::scFullScreen,
					VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Depth);

	GPass.BuildRenderStage();
}

/////////////////////////////////////
// Renderer GPass Functions
/////////////////////////////////////

VkPipelineLayout DeferredRenderer::CreateGPassPipelineLayout()
{
	// Material descriptor set
	// {
	// 	// Create material properties DS layout
	// 	DsLayoutBuilder builder {};
	// 	builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, eShaderType::Pixel);
	// 	DsLayoutLightingMaterialProperties = builder.Build();
	// }


	// {
	// 	DsLayoutBuilder builder {};
	// 	builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
	// 	builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
	// 	builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
	// 	// builder.AddBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ShaderType::Vertex);
	// 	DsLayoutGPassMaterial = builder.Build();
	// }

	// {
	// 	DsLayoutBuilder builder {};
	// 	builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
	// 	DsLayoutGPassMaterialAlbedoOnly = builder.Build();
	// }

	return nullptr;
}


VkPipelineLayout DeferredRenderer::CreateGPassSkinnedPipelineLayout()
{
	// {
	// 	DsLayoutBuilder builder {};
	// 	builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
	// 	builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
	// 	builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
	// 	builder.AddBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, eShaderType::Vertex);
	// 	DsLayoutGPassSkinned = builder.Build();
	// }

	return nullptr;
}


void DeferredRenderer::CreateUnlitPipeline()
{
	CreateForwardPass();
	{
		// Unlit pipeline
		gPSOBuild->BeginPipeline(ePipelineName::Unlit);
		gPSOBuild->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));
		gPSOBuild->SetShader(eShaderName::Unlit, {});

		gPSOBuild->UseRenderStage(ForwardPass);
		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetCullMode(eCullMode::Back);

		gPSOBuild->AddImage(0, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(1, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(2, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));

		gPSOBuild->AddBuffer(0, 1, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());

		gPSOBuild->EndPipeline();
	}

	// {
	// 	// Text rendering pipeline
	// 	gPSOBuild->BeginPipeline(ePipelineName::TextRendering);
	// 	// gPSOBuild->SetLayout(ePipelineName::Unlit);

	// 	gPSOBuild->UseRenderStage(ForwardPass);
	// 	gPSOBuild->SetShader(eShaderName::Text, {});
	// 	gPSOBuild->SetCullMode(eCullMode::None);
	// 	gPSOBuild->EndPipeline();
	// }

	{
		// Debug Layer pipeline
		gPSOBuild->BeginPipeline(ePipelineName::DebugLayer);
		gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(DebugLayerPushConstants));
		gPSOBuild->SetShader(eShaderName::Unlit, { ShaderMacro { .pcName = "IS_DEBUG_LAYER", .pcValue = "1" } });
		gPSOBuild->SetVertexType(eVertexType::Slim);
		gPSOBuild->SetRenderLines(true);
		gPSOBuild->SetCullMode(eCullMode::Back);

		gPSOBuild->UseRenderStage(ForwardPass);

		gPSOBuild->AddBuffer(0, 1, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());

		gPSOBuild->EndPipeline();
	}
}


void DeferredRenderer::CreateGPassPipeline()
{
	CreateGPass();

	{
		gPSOBuild->BeginPipeline(ePipelineName::Geometry);
		gPSOBuild->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

		gPSOBuild->UseRenderStage(GPass);
		gPSOBuild->SetShader(eShaderName::Geometry, {});
		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetCullMode(eCullMode::Back);

		// Use a null image for now, custom DS's created by the materials will be bound at render time
		gPSOBuild->AddImage(0, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		// bObjectBuffer
		gPSOBuild->AddBuffer(0, 1, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());


		gPSOBuild->EndPipeline();
	}


	{
		// Normal mapped pipeline
		gPSOBuild->BeginPipeline(ePipelineName::GeometryNormalMaps);
		// Use previous layout
		// gPSOBuild->SetLayout(ePipelineName::Geometry);

		gPSOBuild->UseRenderStage(GPass);
		gPSOBuild->SetShader(eShaderName::Geometry, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" } });
		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetCullMode(eCullMode::Back);

		gPSOBuild->AddImage(0, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(1, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(2, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));

		// bObjectBuffer
		gPSOBuild->AddBuffer(0, 1, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());

		gPSOBuild->EndPipeline();
	}

	{
		// As there is going to be a descriptor set per material, create an equivalent DS layout here to build the
		// pipeline layotu.
		// SizedArray<DescriptorEntry> ds_entries(5);
		// ds_entries.Emplace(DescriptorEntry::AsImage(
		// 	0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm), gSamplerCache->Request({})));
		// ds_entries.Emplace(DescriptorEntry::AsImage(
		// 	1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm), gSamplerCache->Request({})));
		// ds_entries.Emplace(DescriptorEntry::AsImage(
		// 	2, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm), gSamplerCache->Request({})));
		// ds_entries.Emplace(DescriptorEntry::AsBuffer(3, eShaderType::Pixel, &gRenderer->BoneBuffer.GetGpuBuffer(), 0,
		// 											 gRenderer->BoneBuffer.PageSize));
		// std::pair<Hash32, VkDescriptorSetLayout> ds_layout = gDsLayoutCache->Request(ds_entries);

		// Skinned + Normal mapped pipeline
		gPSOBuild->BeginPipeline(ePipelineName::GeometrySkinned);
		gPSOBuild->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

		gPSOBuild->UseRenderStage(GPass);
		gPSOBuild->SetVertexType(eVertexType::Skinned);
		gPSOBuild->SetShader(eShaderName::Geometry, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" },
													  ShaderMacro { .pcName = "USE_SKINNING", .pcValue = "1" } });
		gPSOBuild->SetCullMode(eCullMode::Back);

		gPSOBuild->AddImage(0, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(1, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(2, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));

		// bBoneBuffer
		gPSOBuild->AddBuffer(3, 0, eShaderType::Vertex, &gRenderer->BoneBuffer.GetGpuBuffer(), 0,
							 gRenderer->BoneBuffer.PageSize);

		// bObjectBuffer
		gPSOBuild->AddBuffer(0, 1, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());


		gPSOBuild->EndPipeline();


		pGeometryPipelineName = ePipelineName::Geometry;
	}
}

void DeferredRenderer::DestroyGPassPipeline()
{
	VkDevice device = gRenderer->GetDevice()->Device;

	// Destroy descriptor set layouts

	// if (DsLayoutGPassMaterial) {
	// 	vkDestroyDescriptorSetLayout(device, DsLayoutGPassMaterial, nullptr);
	// 	DsLayoutGPassMaterial = nullptr;
	// }
	// if (DsLayoutGPassSkinned) {
	// 	vkDestroyDescriptorSetLayout(device, DsLayoutGPassSkinned, nullptr);
	// 	DsLayoutGPassSkinned = nullptr;
	// }
	// if (DsLayoutGPassMaterialAlbedoOnly) {
	// 	vkDestroyDescriptorSetLayout(device, DsLayoutGPassMaterialAlbedoOnly, nullptr);
	// 	DsLayoutGPassMaterialAlbedoOnly = nullptr;
	// }

	gPipelineCache->Request(ePipelineName::Geometry).Destroy();
	gPipelineCache->Request(ePipelineName::GeometryNormalMaps).Destroy();
}


void DeferredRenderer::CreateLightingDSLayout()
{
	// Fragment DS

	// DsLayoutBuilder builder {};

	// sDepth
	// builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
	// // sAlbedo
	// builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
	// // sNormal
	// builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);
	// // sShadowDepth
	// builder.AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);

	// builder.AddBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, eShaderType::Pixel);

	// DsLayoutLightingFrag = builder.Build();
}

void DeferredRenderer::CreateLightingPipeline()
{
	// if (DsLayoutLightingFrag == nullptr) {
	// 	CreateLightingDSLayout();
	// }

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


	{
		// Point light pipeline (inside)
		gPSOBuild->BeginPipeline(ePipelineName::LightingInsideVolume);
		gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(LightVertPushConstants));

		gPSOBuild->UseRenderStage(LightPass);
		gPSOBuild->SetTargetBlend(0, lighting_blend);
		gPSOBuild->SetShader(eShaderName::Lighting, {});
		gPSOBuild->SetVertexType(eVertexType::Slim);

		gPSOBuild->SetDepthTest(false);
		gPSOBuild->SetDepthWrite(false);

		gPSOBuild->SetFaceOrder(eFaceOrder::Reverse);
		gPSOBuild->SetCullMode(eCullMode::Back);


		gPSOBuild->AddImageFromTarget(0, 0, eShaderType::Pixel, GPass.GetTarget(eImageFormat::D32_Float),
									  &gRenderer->Swapchain.DepthSampler);
		gPSOBuild->AddImageFromTarget(1, 0, eShaderType::Pixel, GPass.GetTarget(eImageFormat::BGRA8_UNorm),
									  &gRenderer->Swapchain.ColorSampler);
		gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Pixel, GPass.GetTarget(eImageFormat::RGBA16_Float),
									  &gRenderer->Swapchain.NormalsSampler);

		gPSOBuild->AddImageFromTarget(3, 0, eShaderType::Pixel,
									  gShadowRenderer->RenderStage.GetTarget(eImageFormat::D32_Float),
									  &gRenderer->Swapchain.ShadowDepthSampler);

		gPSOBuild->AddBuffer(4, 0, eShaderType::Pixel, &gRenderer->LightBuffer.GetGpuBuffer(), 0,
							 gRenderer->LightBuffer.PageSize);

		gPSOBuild->AddBuffer(0, 1, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());


		gPSOBuild->EndPipeline();
	}

	{
		// Point light pipeline (outside)
		gPSOBuild->BeginPipeline(ePipelineName::LightingOutsideVolume);
		gPSOBuild->SetLayout(ePipelineName::LightingInsideVolume);

		gPSOBuild->UseRenderStage(LightPass);
		gPSOBuild->SetTargetBlend(0, lighting_blend);
		gPSOBuild->SetShader(eShaderName::Lighting, {});
		gPSOBuild->SetVertexType(eVertexType::Slim);

		gPSOBuild->SetDepthTest(false);
		gPSOBuild->SetDepthWrite(false);

		gPSOBuild->SetFaceOrder(eFaceOrder::Reverse);
		gPSOBuild->SetCullMode(eCullMode::Back);

		// Reuse the descriptor sets defined in LightingInsideVolume
		gPSOBuild->ReuseDS(ePipelineName::LightingInsideVolume);

		gPSOBuild->EndPipeline();
	}

	{
		// Directional lighting pipeline
		gPSOBuild->BeginPipeline(ePipelineName::LightingDirectional);
		gPSOBuild->SetLayout(ePipelineName::LightingInsideVolume);

		gPSOBuild->UseRenderStage(LightPass);
		gPSOBuild->SetTargetBlend(0, lighting_blend);
		gPSOBuild->SetShader(eShaderName::Lighting,
							 { ShaderMacro { .pcName = "FX_LIGHT_DIRECTIONAL", .pcValue = "1" } });
		gPSOBuild->SetVertexType(eVertexType::Slim);

		gPSOBuild->SetDepthTest(false);
		gPSOBuild->SetDepthWrite(false);

		gPSOBuild->SetFaceOrder(eFaceOrder::Reverse);
		gPSOBuild->SetCullMode(eCullMode::None);

		// Since the directional light is a triangle built from the screen coordinates, we won't be passing in vertices.
		gPSOBuild->SetFlags(ePSOBuildFlags::NoVertices);

		// Reuse the descriptor sets defined in LightingInsideVolume
		gPSOBuild->ReuseDS(ePipelineName::LightingInsideVolume);

		gPSOBuild->EndPipeline();
	}
}

void DeferredRenderer::DestroyLightingPipeline()
{
	// VkDevice device = gRenderer->GetDevice()->Device;

	// Destroy descriptor set layouts
	// if (DsLayoutLightingFrag != nullptr) {
	// 	vkDestroyDescriptorSetLayout(device, DsLayoutLightingFrag, nullptr);
	// 	DsLayoutLightingFrag = nullptr;
	// }

	// if (DsLayoutLightingMaterialProperties != nullptr) {
	// 	vkDestroyDescriptorSetLayout(device, DsLayoutLightingMaterialProperties, nullptr);
	// 	DsLayoutLightingMaterialProperties = nullptr;
	// }
}

//////////////////////////////////////////
// DeferredRenderer CompPass Functions
//////////////////////////////////////////

void DeferredRenderer::CreateCompPipeline()
{
	// {
	// 	DsLayoutBuilder builder {};

	// 	builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel)
	// 		.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel)
	// 		.AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel)
	// 		.AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, eShaderType::Pixel);

	// 	DsLayoutCompFrag = builder.Build();
	// }


	// Create composition render stage

	CompPass.Create("Compose", gRenderer->Swapchain.Extent);

	CompPass.MarkFinalStage();
	CompPass.BuildRenderStage();

	// Create composition pipeline

	gPSOBuild->BeginPipeline(ePipelineName::Composition);
	// gPSOBuild->SetPushConstants(eShaderType::Pixel, sizeof(CompositionPushConstants));

	gPSOBuild->UseRenderStage(CompPass);
	gPSOBuild->SetShader(eShaderName::Composition, {});
	gPSOBuild->SetFlags(ePSOBuildFlags::NoVertices);
	gPSOBuild->SetCullMode(eCullMode::None);
	gPSOBuild->SetFaceOrder(eFaceOrder::Default);
	gPSOBuild->SetDepthTest(false);
	gPSOBuild->SetDepthWrite(false);

	gPSOBuild->AddImageFromTarget(1, 0, eShaderType::Pixel, GPass.GetTarget(eImageFormat::D32_Float),
								  gSamplerCache->Request(SamplerProps {
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
								  }));
	gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Pixel, GPass.GetTarget(eImageFormat::BGRA8_UNorm),
								  gSamplerCache->Request(SamplerProps {}));
	gPSOBuild->AddImageFromTarget(3, 0, eShaderType::Pixel, GPass.GetTarget(eImageFormat::RGBA16_Float),
								  gSamplerCache->Request(SamplerProps {}));
	gPSOBuild->AddImageFromTarget(4, 0, eShaderType::Pixel, LightPass.GetTarget(eImageFormat::RGBA16_Float),
								  gSamplerCache->Request(SamplerProps {}));

	gPSOBuild->EndPipeline();
}

void DeferredRenderer::DestroyCompPipeline()
{
	VkDevice device = gRenderer->GetDevice()->Device;

	// Destroy descriptor set layouts
	// if (DsLayoutCompFrag) {
	// 	vkDestroyDescriptorSetLayout(device, DsLayoutCompFrag, nullptr);
	// 	DsLayoutCompFrag = nullptr;
	// }
}

void DeferredRenderer::CreateDescriptorSets()
{
	// DsComposition.Destroy();
	// DsComposition.Create(DescriptorPool, DsLayoutCompFrag, false);
	// DsComposition.AddImageFromTarget(1, GPass.GetTarget(eImageFormat::D32_Float),
	// 								 gSamplerCache->Request(SamplerProps {
	// 									 eSamplerFilter::Nearest,
	// 									 eSamplerFilter::Nearest,
	// 									 eSamplerFilter::Nearest,
	// 								 }));
	// DsComposition.AddImageFromTarget(2, GPass.GetTarget(eImageFormat::BGRA8_UNorm),
	// 								 gSamplerCache->Request(SamplerProps {}));
	// DsComposition.AddImageFromTarget(3, GPass.GetTarget(eImageFormat::RGBA16_Float),
	// 								 gSamplerCache->Request(SamplerProps {}));
	// DsComposition.AddImageFromTarget(4, LightPass.GetTarget(eImageFormat::RGBA16_Float),
	// 								 gSamplerCache->Request(SamplerProps {}));
	// DsComposition.Build();


	// DsLighting.Destroy();
	// DsLighting.Create(DescriptorPool, DsLayoutLightingFrag, true);
	// // sDepth
	// DsLighting.AddImageFromTarget(0, GPass.GetTarget(eImageFormat::D32_Float), &gRenderer->Swapchain.DepthSampler);
	// // sAlbedo
	// DsLighting.AddImageFromTarget(1, GPass.GetTarget(eImageFormat::BGRA8_UNorm), &gRenderer->Swapchain.ColorSampler);

	// // sNormals
	// DsLighting.AddImageFromTarget(2, GPass.GetTarget(eImageFormat::RGBA16_Float),
	// &gRenderer->Swapchain.NormalsSampler);

	// if (gShadowRenderer != nullptr && gShadowRenderer->RenderStage.IsBuilt()) {
	// 	DsLighting.AddImageFromTarget(3, gShadowRenderer->RenderStage.GetTarget(eImageFormat::D32_Float),
	// 								  &gRenderer->Swapchain.ShadowDepthSampler);
	// }

	// // Skip 3 for the shadow target, added by DirectionalShadows
	// DsLighting.AddBuffer(4, &gRenderer->LightBuffer.GetGpuBuffer(), 0, gRenderer->LightBuffer.PageSize);

	// DsLighting.Build();
}

void DeferredRenderer::CreateCompPass() {}

void DeferredRenderer::DoCompPass(Camera& camera)
{
	CommandBuffer& cmd = gRenderer->GetFrame()->CmdBuffer;

	// CompositionPushConstants push_constants {};
	// memcpy(push_constants.ViewInverse, camera.InvViewMatrix.RawData, sizeof(Mat4f));
	// memcpy(push_constants.ProjInverse, camera.InvProjectionMatrix.RawData, sizeof(Mat4f));

	// Pipeline& composition_pipeline = gPipelineCache->Request(ePipelineName::Composition);

	// gRenderer->SubmitPushConstants(cmd, composition_pipeline, eShaderType::Pixel, push_constants);

	gPipelineCache->Bind(ePipelineName::Composition, cmd);

	// DsComposition.Bind(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composition_pipeline);

	// Use single triangle instead of two triangles as it removes the overlapping quads the gpu
	// renders between triangles. Source: https://wallisc.github.io/rendering/2021/04/18/Fullscreen-Pass.html
	vkCmdDraw(cmd.Get(), 3, 1, 0, 0);
}

} // namespace fx::renderer
