#include "DeferredRenderer.hpp"

#include "Backend/BarrierHelper.hpp"
#include "Backend/Commands.hpp"
#include "Backend/DescriptorCache.hpp"
#include "Backend/DsLayoutBuilder.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "Backend/Shader.hpp"
#include "Backend/VertexDescription.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "Globals.hpp"
#include "Limits.hpp"
#include "PSOBuild.hpp"
#include "PipelineCache.hpp"
#include "RenderBackend.hpp"
#include "ShaderCache.hpp"
#include "ShadowDirectional.hpp"

#include <Asset/AssetManager.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/ObjectManager.hpp>
#include <algorithm>

/*

General Descriptor Sets
+===============================================================================+
| 0  | ObjectBuffer, MaterialBuffer, Light Buffers (globals, persistent)        |
+-------------------------------------------------------------------------------+
| 1  | Material images + Bones                                                  |
+===============================================================================+

 */

namespace fx::renderer {

FX_SET_MODULE_NAME("DeferredRenderer")

/// Descriptor set index that holds the Forward+ tiled light lists
static constexpr uint32 scLightGridSetIndex = 2;

void DeferredRenderer::Create(const Vec2u& extent)
{
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10);
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 20);
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 15);
	DescriptorPool.Create(gRenderer->GetDevice(), 16);

	CreateGPassPipeline();
	// CreateLightingPipeline();
	CreateCompPipeline();
	CreateLightCullingPipeline();

	BuildPersistentDescriptor();

	// CreateUnlitPipeline();
	//
}

void DeferredRenderer::Destroy() {}

void DeferredRenderer::CreateUnlitPass()
{
	// TargetList targets {};

	// Target* lp_light_attachment = LightPass.GetTarget(eImageFormat::RGBA16_Float);
	// Target* lp_depth_attachment = GPass.GetTarget(eImageFormat::D32_Float);

	// Assert(lp_light_attachment != nullptr && lp_depth_attachment != nullptr);

	// UnlitPass.Create("Unlit", gRenderer->Swapchain.Extent);

	// UnlitPass.AddTarget(eImageFormat::D32_Float, Target::scFullScreen,
	// 					VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	// 					eImageAspectFlag::Depth);
	// {
	// 	Target* depth_target = UnlitPass.GetTarget(eImageFormat::D32_Float);
	// 	depth_target->LoadOp = eLoadOp::Load;
	// 	depth_target->StoreOp = eStoreOp::DontCare;
	// 	depth_target->InitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	// 	depth_target->UseImageFromTarget(lp_depth_attachment);
	// }

	// UnlitPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
	// 					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);
	// {
	// 	Target* light_target = UnlitPass.GetTarget(eImageFormat::RGBA16_Float);
	// 	light_target->LoadOp = eLoadOp::Load;
	// 	light_target->StoreOp = eStoreOp::Store;
	// 	light_target->InitialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	// 	light_target->FinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	// 	light_target->UseImageFromTarget(lp_light_attachment);
	// }

	// UnlitPass.BuildRenderStage();
}

void DeferredRenderer::CreateGPass()
{
	// GPass.Create("Geometry", gRenderer->Swapchain.Extent);

	// // Albedo target
	// GPass.AddTarget(eImageFormat::BGRA8_UNorm, Target::scFullScreen,
	// 				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

	// // Normals target
	// GPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
	// 				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

	// // Depth target
	// GPass.AddTarget(eImageFormat::D32_Float, Target::scFullScreen,
	// 				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Depth);

	// GPass.BuildRenderStage();

	// Forward pass
	ForwardPass.Create("Forward", gRenderer->Swapchain.Extent);

	// Lit target
	ForwardPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
						  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

	// Depth target
	ForwardPass.AddTarget(eImageFormat::D32_Float, Target::scFullScreen,
						  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						  eImageAspectFlag::Depth);

	ForwardPass.BuildRenderStage();
}

/////////////////////////////////////
// Renderer GPass Functions
/////////////////////////////////////


void DeferredRenderer::CreateUnlitPipeline()
{
	// CreateUnlitPass();
	// {
	// 	// Unlit pipeline
	// 	gPSOBuild->BeginPipeline(ePipelineName::Unlit);
	// 	gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(DrawPushConstants));
	// 	gPSOBuild->SetShader(eShaderName::Unlit, {});

	// 	gPSOBuild->UseRenderStage(UnlitPass);
	// 	gPSOBuild->SetVertexType(eVertexType::Default);
	// 	gPSOBuild->SetCullMode(eCullMode::Back);

	// 	gPSOBuild->AddImage(0, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
	// 						gSamplerCache->Request({}));
	// 	// gPSOBuild->AddImage(1, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
	// 	// 					gSamplerCache->Request({}));
	// 	// gPSOBuild->AddImage(2, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
	// 	// 					gSamplerCache->Request({}));

	// 	gPSOBuild->AddBuffer(0, 1, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
	// 						 gObjectManager->GetPageSize());

	// 	gPSOBuild->EndPipeline();


	// 	// Unlit pipeline
	// 	gPSOBuild->BeginPipeline(ePipelineName::UnlitNormalMaps);
	// 	gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(DrawPushConstants));
	// 	gPSOBuild->SetShader(eShaderName::Unlit, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" } });

	// 	gPSOBuild->UseRenderStage(UnlitPass);
	// 	gPSOBuild->SetVertexType(eVertexType::Default);
	// 	gPSOBuild->SetCullMode(eCullMode::Back);

	// 	gPSOBuild->AddImage(0, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
	// 						gSamplerCache->Request({}));
	// 	gPSOBuild->AddImage(1, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
	// 						gSamplerCache->Request({}));
	// 	gPSOBuild->AddImage(2, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
	// 						gSamplerCache->Request({}));

	// 	gPSOBuild->AddBuffer(0, 1, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
	// 						 gObjectManager->GetPageSize());

	// 	gPSOBuild->EndPipeline();
	// }

	// {
	// 	// Debug Layer pipeline
	// 	gPSOBuild->BeginPipeline(ePipelineName::DebugLayer);
	// 	gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(DebugLayerPushConstants));
	// 	gPSOBuild->SetShader(eShaderName::Unlit, { ShaderMacro { .pcName = "IS_DEBUG_LAYER", .pcValue = "1" } });
	// 	gPSOBuild->SetVertexType(eVertexType::Slim);
	// 	gPSOBuild->SetRenderLines(true);
	// 	gPSOBuild->SetCullMode(eCullMode::Back);

	// 	gPSOBuild->UseRenderStage(UnlitPass);

	// 	gPSOBuild->AddBuffer(0, 1, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
	// 						 gObjectManager->GetPageSize());

	// 	gPSOBuild->EndPipeline();
	// }
}

void DeferredRenderer::BuildPersistentDescriptor()
{
	SizedArray<DescriptorEntry> ds_entries(6);

	ds_entries.Insert(DescriptorEntry::AsBuffer(0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
												ObjectManager::scBoundSize));

	ds_entries.Insert(DescriptorEntry::AsBuffer(1, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
												gMaterialManager->MaterialPropertiesBuffer.Size));


	std::pair<DescriptorID, DescriptorSet*> result = gDescriptorCache->Request(ds_entries);
	pPersistentDescriptorSlim = result.second;

	// Add the other descriptors for the non-slim

	ds_entries.Insert(
		DescriptorEntry::AsBuffer(2, eShaderType::Pixel, &gRenderer->LightGridBuffer, 0, gRenderer->LightGridPageSize));

	ds_entries.Insert(DescriptorEntry::AsBuffer(3, eShaderType::Pixel, &gRenderer->LightIndexListBuffer, 0,
												gRenderer->LightIndexListPageSize));

	Target* shadow_target = gShadowRenderer->RenderStage.GetTarget(eImageFormat::D32_Float);
	Assert(shadow_target != nullptr);

	ds_entries.Insert(DescriptorEntry::AsImage(4, eShaderType::Pixel, &shadow_target->Image,
											   gSamplerCache->Request({
												   eSamplerFilter::Linear,
												   eSamplerFilter::Linear,
												   eSamplerFilter::Linear,
												   eSamplerAddressMode::ClampToBorder,
												   eSamplerBorderColor::FloatWhite,
												   eSamplerCompareOp::Greater,
											   })));

	result = gDescriptorCache->Request(ds_entries);
	pPersistentDescriptor = result.second;
}


void DeferredRenderer::CreateGPassPipeline()
{
	CreateGPass();

	{
		gPSOBuild->BeginPipeline(ePipelineName::Geometry);
		gPSOBuild->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

		gPSOBuild->UseRenderStage(ForwardPass);
		gPSOBuild->SetShader(eShaderName::Forward, {});
		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetCullMode(eCullMode::Back);

		// Set 0 (Global / Per Frame)

		// bObjectBuffer
		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());
		// bMaterialBuffer
		gPSOBuild->AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
							 gMaterialManager->MaterialPropertiesBuffer.Size);
		// bLightGrid
		gPSOBuild->AddBuffer(2, 0, eShaderType::Pixel, &gRenderer->LightGridBuffer, 0, gRenderer->LightGridPageSize);
		// bLightIndexList
		gPSOBuild->AddBuffer(3, 0, eShaderType::Pixel, &gRenderer->LightIndexListBuffer, 0,
							 gRenderer->LightIndexListPageSize);
		// tShadowAtlas
		gPSOBuild->AddImage(4, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::D32_Float),
							gSamplerCache->Request({}));


		// Set 1 (Object local)

		// Use a null image for now, custom DS's created by the materials will be bound at render time
		gPSOBuild->AddImage(0, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));

		gPSOBuild->AddBuffer(4, 1, eShaderType::Pixel, &gRenderer->LightBuffer.GetGpuBuffer(), 0,
							 gRenderer->LightBuffer.PageSize);


		gPSOBuild->EndPipeline();
	}


	{
		// Normal mapped pipeline
		gPSOBuild->BeginPipeline(ePipelineName::GeometryNormalMaps);
		gPSOBuild->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

		gPSOBuild->UseRenderStage(ForwardPass);
		gPSOBuild->SetShader(eShaderName::Forward, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" } });
		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetCullMode(eCullMode::Back);

		// Set 0 (Global / Per Frame)

		// bObjectBuffer
		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());
		// bMaterialBuffer
		gPSOBuild->AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
							 gMaterialManager->MaterialPropertiesBuffer.Size);

		// bLightGrid
		gPSOBuild->AddBuffer(2, 0, eShaderType::Pixel, &gRenderer->LightGridBuffer, 0, gRenderer->LightGridPageSize);
		// bLightIndexList
		gPSOBuild->AddBuffer(3, 0, eShaderType::Pixel, &gRenderer->LightIndexListBuffer, 0,
							 gRenderer->LightIndexListPageSize);
		// tShadowAtlas
		gPSOBuild->AddImage(4, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::D32_Float),
							gSamplerCache->Request({}));

		// Set 1 (Object local)

		// tAlbedo
		gPSOBuild->AddImage(0, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		// tNormalMap
		gPSOBuild->AddImage(1, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		// tMetallicRoughness
		gPSOBuild->AddImage(2, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));

		// FSLightBuffer
		gPSOBuild->AddBuffer(4, 1, eShaderType::Pixel, &gRenderer->LightBuffer.GetGpuBuffer(), 0,
							 gRenderer->LightBuffer.PageSize);


		gPSOBuild->EndPipeline();
	}

	{
		// Skinned + Normal mapped pipeline
		gPSOBuild->BeginPipeline(ePipelineName::GeometrySkinned);
		gPSOBuild->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

		gPSOBuild->UseRenderStage(ForwardPass);
		gPSOBuild->SetVertexType(eVertexType::Skinned);
		gPSOBuild->SetShader(eShaderName::Forward, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" },
													 ShaderMacro { .pcName = "USE_SKINNING", .pcValue = "1" } });
		gPSOBuild->SetCullMode(eCullMode::Back);

		// Set 0 (Global / Per Frame)

		// bObjectBuffer
		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());

		// bMaterialBuffer
		gPSOBuild->AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
							 gMaterialManager->MaterialPropertiesBuffer.Size);

		// bLightGrid
		gPSOBuild->AddBuffer(2, 0, eShaderType::Pixel, &gRenderer->LightGridBuffer, 0, gRenderer->LightGridPageSize);
		// bLightIndexList
		gPSOBuild->AddBuffer(3, 0, eShaderType::Pixel, &gRenderer->LightIndexListBuffer, 0,
							 gRenderer->LightIndexListPageSize);
		// tShadowAtlas
		gPSOBuild->AddImage(4, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::D32_Float),
							gSamplerCache->Request({}));


		// Set 1 (Object local)

		gPSOBuild->AddImage(0, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(1, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(2, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		// bBoneBuffer
		gPSOBuild->AddBuffer(3, 1, eShaderType::Vertex, &gRenderer->BoneBuffer.GetGpuBuffer(), 0,
							 gRenderer->BoneBuffer.PageSize);
		// Light buffer
		gPSOBuild->AddBuffer(4, 1, eShaderType::Pixel, &gRenderer->LightBuffer.GetGpuBuffer(), 0,
							 gRenderer->LightBuffer.PageSize);

		gPSOBuild->EndPipeline();


		pGeometryPipelineName = ePipelineName::Geometry;
	}
}

void DeferredRenderer::AddLightGridDescriptors() {}

void DeferredRenderer::CreateLightCullingPipeline()
{
	gPSOBuild->BeginPipeline(ePipelineName::LightCulling);
	gPSOBuild->SetPushConstants(eShaderType::Compute, sizeof(LightCullPushConstants));
	gPSOBuild->SetShader(eShaderName::LightCulling, {});

	// bLightGrid
	gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &gRenderer->LightGridBuffer, 0, gRenderer->LightGridPageSize);
	// bLightIndexList
	gPSOBuild->AddBuffer(1, 0, eShaderType::Compute, &gRenderer->LightIndexListBuffer, 0,
						 gRenderer->LightIndexListPageSize);
	// FSLightBuffer
	gPSOBuild->AddBuffer(4, 0, eShaderType::Compute, &gRenderer->LightBuffer.GetGpuBuffer(), 0,
						 gRenderer->LightBuffer.PageSize);

	gPSOBuild->EndPipeline();
}

void DeferredRenderer::DoLightCullingPass(Camera& camera)
{
	CommandBuffer& cmd = gRenderer->GetFrame()->CmdBuffer;

	const Vec2u extent = gRenderer->Swapchain.Extent;

	const uint32 tile_columns = std::min((extent.X + (Limits::LightTileSize - 1)) / Limits::LightTileSize,
										 Limits::MaxScreenTilesX);
	const uint32 tile_rows = std::min((extent.Y + (Limits::LightTileSize - 1)) / Limits::LightTileSize,
									  Limits::MaxScreenTilesY);

	mLightTileColumns = tile_columns;

	const Mat4f& cam_matrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);

	LightCullPushConstants push_constants {};
	memcpy(push_constants.CameraMatrix, cam_matrix.RawData, sizeof(Mat4f));
	push_constants.ScreenSize[0] = static_cast<float32>(extent.X);
	push_constants.ScreenSize[1] = static_cast<float32>(extent.Y);

	push_constants.LightCount = gRenderer->LightBuffer.SlotIndex;
	push_constants.TileColumns = tile_columns;

	gPipelineCache->AddBufferOffset(0, gRenderer->GetLightGridFrameOffset());
	gPipelineCache->AddBufferOffset(0, gRenderer->GetLightIndexListFrameOffset());
	gPipelineCache->AddBufferOffset(0, gRenderer->LightBuffer.GetBaseOffset());
	gPipelineCache->Bind(ePipelineName::LightCulling, cmd);

	gRenderer->SubmitPushConstants(cmd, gPipelineCache->Request(ePipelineName::LightCulling), eShaderType::Compute,
								   push_constants);

	vkCmdDispatch(cmd.Get(), tile_columns, tile_rows, 1);

	// Make the culled light lists visible to the fragment shader
	BarrierHelper::BufferComputeToFragment(cmd, &gRenderer->LightGridBuffer);
	BarrierHelper::BufferComputeToFragment(cmd, &gRenderer->LightIndexListBuffer);
}

void DeferredRenderer::BindLightGridDescriptors(CommandBuffer& cmd)
{
	Pipeline& pipeline = gPipelineCache->Request(ePipelineName::Geometry);

	const uint32 buffer_offsets[] = { gRenderer->GetLightGridFrameOffset(), gRenderer->GetLightIndexListFrameOffset() };

	for (Pipeline::DescriptorRef& desc_ref : pipeline.DescriptorIDs) {
		if (desc_ref.SetIndex != scLightGridSetIndex) {
			continue;
		}

		desc_ref.pSet->Bind(scLightGridSetIndex, cmd, pipeline,
							Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
		return;
	}
}


void DeferredRenderer::CreateLightingPipeline()
{
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
		gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(LightVertPushConstants));
		gPSOBuild->SetLayout(ePipelineName::LightingInsideVolume);

		gPSOBuild->UseRenderStage(LightPass);
		gPSOBuild->SetTargetBlend(0, lighting_blend);
		gPSOBuild->SetShader(eShaderName::Lighting, {});
		gPSOBuild->SetVertexType(eVertexType::Slim);

		gPSOBuild->SetDepthTest(false);
		gPSOBuild->SetDepthWrite(false);

		gPSOBuild->SetFaceOrder(eFaceOrder::Reverse);
		gPSOBuild->SetCullMode(eCullMode::Back);

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

		gPSOBuild->EndPipeline();
	}
}

//////////////////////////////////////////
// DeferredRenderer CompPass Functions
//////////////////////////////////////////

void DeferredRenderer::CreateCompPipeline()
{
	// Create composition render stage

	CompPass.Create("Compose", gRenderer->Swapchain.Extent);

	CompPass.MarkFinalStage();
	CompPass.BuildRenderStage();

	// Create composition pipeline

	gPSOBuild->BeginPipeline(ePipelineName::Composition);

	gPSOBuild->UseRenderStage(CompPass);
	gPSOBuild->SetShader(eShaderName::Composition, {});
	gPSOBuild->SetFlags(ePSOBuildFlags::NoVertices);
	gPSOBuild->SetCullMode(eCullMode::None);
	gPSOBuild->SetFaceOrder(eFaceOrder::Default);
	gPSOBuild->SetDepthTest(false);
	gPSOBuild->SetDepthWrite(false);

	gPSOBuild->AddImageFromTarget(1, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::D32_Float),
								  gSamplerCache->Request(SamplerProps {
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
								  }));
	gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::RGBA16_Float),
								  gSamplerCache->Request(SamplerProps {}));

	gPSOBuild->EndPipeline();
}

void DeferredRenderer::DoCompPass(Camera& camera)
{
	CommandBuffer& cmd = gRenderer->GetFrame()->CmdBuffer;

	gPipelineCache->Bind(ePipelineName::Composition, cmd);

	// Use single triangle instead of two triangles as it removes the overlapping quads the gpu
	// renders between triangles. Source: https://wallisc.github.io/rendering/2021/04/18/Fullscreen-Pass.html
	vkCmdDraw(cmd.Get(), 3, 1, 0, 0);
}

} // namespace fx::renderer
