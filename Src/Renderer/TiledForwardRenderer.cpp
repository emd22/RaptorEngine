#include "TiledForwardRenderer.hpp"

#include "Backend/BarrierHelper.hpp"
#include "Backend/Commands.hpp"
#include "Backend/DescriptorCache.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "Backend/Shader.hpp"
#include "Camera.hpp"
#include "Engine.hpp"
#include "Globals.hpp"
#include "GraphicsBackend.hpp"
#include "Limits.hpp"
#include "PSOBuild.hpp"
#include "PipelineCache.hpp"
#include "ShadowDirectional.hpp"

#include <Asset/AssetManager.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/ObjectManager.hpp>
#include <Texture/TextureManager.hpp>
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

void TiledForwardRenderer::Create(const Vec2u& extent)
{
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10);
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 20);
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 15);
	DescriptorPool.Create(gGraphics->GetDevice(), 16);

	CreateDepthNormalPSO();
	CreateSSAOPSO();
	CreateForwardPSO();
	CreateCompositionPSO();
	CreateLightCullingPSO();
	CreateDebugLayerPSO();


	BuildPersistentDescriptor();
}

void TiledForwardRenderer::Destroy() {}

void TiledForwardRenderer::CreateForwardPass()
{
	// Forward pass
	ForwardPass.Create("Forward", gGraphics->Swapchain.Extent);

	// Lit target
	ForwardPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
						  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

	// Depth target
	ForwardPass.AddTarget(eImageFormat::D32_Float, Target::scFullScreen,
						  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						  eImageAspectFlag::Depth);

	ForwardPass.BuildRenderStage();
}

void TiledForwardRenderer::CreateDepthNormalPass()
{
	// Depth + normal prepass
	Prepass.Create("DepthNormal", gGraphics->Swapchain.Extent);

	// Depth target (index 0)
	Prepass.AddTarget(eImageFormat::D32_Float, Target::scFullScreen,
					  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					  eImageAspectFlag::Depth);

	// World-space normals target (index 1)
	Prepass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
					  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

	Prepass.BuildRenderStage();
}

void TiledForwardRenderer::CreateSSAOPass()
{
	SSAOPass.Create("Forward", gGraphics->Swapchain.Extent);

	// SSAO output target
	SSAOPass.AddTarget(eImageFormat::R8_UInt, Target::scFullScreen,
					   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

	SSAOPass.BuildRenderStage();
}


void TiledForwardRenderer::BuildPersistentDescriptor()
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
		DescriptorEntry::AsBuffer(2, eShaderType::Pixel, &gGraphics->LightGridBuffer, 0, gGraphics->LightGridPageSize));

	ds_entries.Insert(DescriptorEntry::AsBuffer(3, eShaderType::Pixel, &gGraphics->LightIndexListBuffer, 0,
												gGraphics->LightIndexListPageSize));

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

	Target* ssao_target = SSAOPass.GetTarget(eImageFormat::R8_UInt);
	Assert(ssao_target != nullptr);

	ds_entries.Insert(DescriptorEntry::AsImage(5, eShaderType::Pixel, &ssao_target->Image,
											   gSamplerCache->Request({
												   eSamplerFilter::Nearest,
												   eSamplerFilter::Nearest,
												   eSamplerFilter::Nearest,
											   })));

	result = gDescriptorCache->Request(ds_entries);
	pPersistentDescriptor = result.second;
}

void TiledForwardRenderer::CreateSSAOPSO()
{
	CreateSSAOPass();

	{
		gPSOBuild->BeginPipeline(ePipelineName::SSAO);

		gPSOBuild->UseRenderStage(SSAOPass);
		gPSOBuild->SetShader(eShaderName::SSAO, {});
		gPSOBuild->SetFlags(ePSOBuildFlags::NoVertices);
		gPSOBuild->SetCullMode(eCullMode::None);

		gPSOBuild->SetPushConstants(eShaderType::Pixel, sizeof(SSAOPushConsts));

		// Set 0 (Global / Per Frame)
		gPSOBuild->AddImageFromTarget(0, 0, eShaderType::Pixel, Prepass.GetTarget(eImageFormat::D32_Float),
									  gSamplerCache->Request(SamplerProps {
										  eSamplerFilter::Nearest,
										  eSamplerFilter::Nearest,
										  eSamplerFilter::Nearest,
									  }));

		// tNormal
		gPSOBuild->AddImageFromTarget(1, 0, eShaderType::Pixel, Prepass.GetTarget(eImageFormat::RGBA16_Float),
									  gSamplerCache->Request({}));
		// tNoise
		gPSOBuild->AddImage(2, 0, eShaderType::Pixel, gGraphics->pNoiseTexture,
							gSamplerCache->Request({
								.MinFilter = eSamplerFilter::Nearest,
								.MagFilter = eSamplerFilter::Nearest,
								.MipFilter = eSamplerFilter::Nearest,
							}));


		gPSOBuild->EndPipeline();
	}
}

void TiledForwardRenderer::CreateDebugLayerPSO()
{
	// Debug Layer pipeline
	gPSOBuild->BeginPipeline(ePipelineName::DebugLayer);
	gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(DebugLayerPushConstants));

	gPSOBuild->UseRenderStage(ForwardPass);
	gPSOBuild->SetShader(eShaderName::DebugLayer, {});
	gPSOBuild->SetVertexType(eVertexType::Slim);
	gPSOBuild->SetRenderLines(true);
	gPSOBuild->SetCullMode(eCullMode::Back);
	gPSOBuild->EndPipeline();
}


void TiledForwardRenderer::CreateForwardPSO()
{
	CreateForwardPass();

	{
		gPSOBuild->BeginPipeline(ePipelineName::Geometry);
		gPSOBuild->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

		gPSOBuild->UseRenderStage(ForwardPass);
		gPSOBuild->SetShader(eShaderName::Forward, {});
		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetCullMode(eCullMode::Back);

		BlendAttachment blend = BlendAttachment {
			.Enabled = true,
			.BlendOp = {
				.Ops = {
					.Alpha = VK_BLEND_OP_ADD,
					.Color = VK_BLEND_OP_ADD,
				},
			},
			.AlphaBlend { .Ops {
				.Src = VK_BLEND_FACTOR_ONE,
				.Dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			} },
			.ColorBlend { .Ops { .Src = VK_BLEND_FACTOR_SRC_ALPHA, .Dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA } },
		};

		gPSOBuild->SetTargetBlend(ForwardPass.GetTargetIndex(eImageFormat::RGBA16_Float), blend);

		// Set 0 (Global / Per Frame)

		// bObjectBuffer
		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());
		// bMaterialBuffer
		gPSOBuild->AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
							 gMaterialManager->MaterialPropertiesBuffer.Size);
		// bLightGrid
		gPSOBuild->AddBuffer(2, 0, eShaderType::Pixel, &gGraphics->LightGridBuffer, 0, gGraphics->LightGridPageSize);
		// bLightIndexList
		gPSOBuild->AddBuffer(3, 0, eShaderType::Pixel, &gGraphics->LightIndexListBuffer, 0,
							 gGraphics->LightIndexListPageSize);
		// tShadowAtlas
		gPSOBuild->AddImage(4, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::D32_Float),
							gSamplerCache->Request({}));
		// tSSAO
		gPSOBuild->AddImageFromTarget(5, 0, eShaderType::Pixel, SSAOPass.GetTarget(eImageFormat::R8_UInt),
									  gSamplerCache->Request({
										  .MinFilter = eSamplerFilter::Nearest,
										  .MagFilter = eSamplerFilter::Nearest,
										  .MipFilter = eSamplerFilter::Nearest,
									  }));


		// Set 1 (Object local)

		// Use a null image for now, custom DS's created by the materials will be bound at render time
		gPSOBuild->AddImage(0, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));

		gPSOBuild->AddBuffer(4, 1, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
							 gGraphics->LightBuffer.PageSize);


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
		gPSOBuild->AddBuffer(2, 0, eShaderType::Pixel, &gGraphics->LightGridBuffer, 0, gGraphics->LightGridPageSize);
		// bLightIndexList
		gPSOBuild->AddBuffer(3, 0, eShaderType::Pixel, &gGraphics->LightIndexListBuffer, 0,
							 gGraphics->LightIndexListPageSize);
		// tShadowAtlas
		gPSOBuild->AddImage(4, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::D32_Float),
							gSamplerCache->Request({}));
		// tSSAO
		gPSOBuild->AddImageFromTarget(5, 0, eShaderType::Pixel, SSAOPass.GetTarget(eImageFormat::R8_UInt),
									  gSamplerCache->Request({
										  .MinFilter = eSamplerFilter::Nearest,
										  .MagFilter = eSamplerFilter::Nearest,
										  .MipFilter = eSamplerFilter::Nearest,
									  }));

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
		gPSOBuild->AddBuffer(4, 1, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
							 gGraphics->LightBuffer.PageSize);


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
		gPSOBuild->AddBuffer(2, 0, eShaderType::Pixel, &gGraphics->LightGridBuffer, 0, gGraphics->LightGridPageSize);
		// bLightIndexList
		gPSOBuild->AddBuffer(3, 0, eShaderType::Pixel, &gGraphics->LightIndexListBuffer, 0,
							 gGraphics->LightIndexListPageSize);
		// tShadowAtlas
		gPSOBuild->AddImage(4, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::D32_Float),
							gSamplerCache->Request({}));

		// tSSAO
		gPSOBuild->AddImageFromTarget(5, 0, eShaderType::Pixel, SSAOPass.GetTarget(eImageFormat::R8_UInt),
									  gSamplerCache->Request({
										  .MinFilter = eSamplerFilter::Nearest,
										  .MagFilter = eSamplerFilter::Nearest,
										  .MipFilter = eSamplerFilter::Nearest,
									  }));


		// Set 1 (Object local)

		gPSOBuild->AddImage(0, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(1, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(2, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		// bBoneBuffer
		gPSOBuild->AddBuffer(3, 1, eShaderType::Vertex, &gGraphics->BoneBuffer.GetGpuBuffer(), 0,
							 gGraphics->BoneBuffer.PageSize);
		// Light buffer
		gPSOBuild->AddBuffer(4, 1, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
							 gGraphics->LightBuffer.PageSize);

		gPSOBuild->EndPipeline();


		pGeometryPipelineName = ePipelineName::Geometry;
	}
}

void TiledForwardRenderer::CreateDepthNormalPSO()
{
	CreateDepthNormalPass();

	{
		gPSOBuild->BeginPipeline(ePipelineName::DepthNormal);
		gPSOBuild->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

		gPSOBuild->UseRenderStage(Prepass);
		gPSOBuild->SetShader(eShaderName::DepthNormal, {});
		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetCullMode(eCullMode::Back);

		// Set 0 (Global / Per Frame)

		// bObjectBuffer
		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());
		// bMaterialBuffer
		gPSOBuild->AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
							 gMaterialManager->MaterialPropertiesBuffer.Size);

		// Set 1 (Object local)

		// tAlbedo
		gPSOBuild->AddImage(0, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		// FSLightBuffer (declared to match the material descriptor set layout used by Geometry)
		gPSOBuild->AddBuffer(4, 1, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
							 gGraphics->LightBuffer.PageSize);

		gPSOBuild->EndPipeline();
	}

	{
		// Normal mapped prepass pipeline
		gPSOBuild->BeginPipeline(ePipelineName::DepthNormalNormalMaps);
		gPSOBuild->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

		gPSOBuild->UseRenderStage(Prepass);
		gPSOBuild->SetShader(eShaderName::DepthNormal, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" } });
		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetCullMode(eCullMode::Back);

		// Set 0 (Global / Per Frame)

		// bObjectBuffer
		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());
		// bMaterialBuffer
		gPSOBuild->AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
							 gMaterialManager->MaterialPropertiesBuffer.Size);

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
		// FSLightBuffer (declared to match the material descriptor set layout used by GeometryNormalMaps)
		gPSOBuild->AddBuffer(4, 1, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
							 gGraphics->LightBuffer.PageSize);

		gPSOBuild->EndPipeline();
	}

	{
		// Skinned + Normal mapped prepass pipeline
		gPSOBuild->BeginPipeline(ePipelineName::DepthNormalSkinned);
		gPSOBuild->SetPushConstants(eShaderType::Vertex | eShaderType::Pixel, sizeof(DrawPushConstants));

		gPSOBuild->UseRenderStage(Prepass);
		gPSOBuild->SetVertexType(eVertexType::Skinned);
		gPSOBuild->SetShader(eShaderName::DepthNormal, { ShaderMacro { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" },
														 ShaderMacro { .pcName = "USE_SKINNING", .pcValue = "1" } });
		gPSOBuild->SetCullMode(eCullMode::Back);

		// Set 0 (Global / Per Frame)

		// bObjectBuffer
		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());
		// bMaterialBuffer
		gPSOBuild->AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
							 gMaterialManager->MaterialPropertiesBuffer.Size);

		// Set 1 (Object local)

		gPSOBuild->AddImage(0, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(1, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddImage(2, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		// bBoneBuffer
		gPSOBuild->AddBuffer(3, 1, eShaderType::Vertex, &gGraphics->BoneBuffer.GetGpuBuffer(), 0,
							 gGraphics->BoneBuffer.PageSize);
		// FSLightBuffer (declared to match the material descriptor set layout used by GeometrySkinned)
		gPSOBuild->AddBuffer(4, 1, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
							 gGraphics->LightBuffer.PageSize);

		gPSOBuild->EndPipeline();
	}
}

void TiledForwardRenderer::AddLightGridDescriptors() {}

void TiledForwardRenderer::CreateLightCullingPSO()
{
	gPSOBuild->BeginPipeline(ePipelineName::LightCulling);
	gPSOBuild->SetPushConstants(eShaderType::Compute, sizeof(LightCullPushConstants));
	gPSOBuild->SetShader(eShaderName::LightCulling, {});

	// bLightGrid
	gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &gGraphics->LightGridBuffer, 0, gGraphics->LightGridPageSize);
	// bLightIndexList
	gPSOBuild->AddBuffer(1, 0, eShaderType::Compute, &gGraphics->LightIndexListBuffer, 0,
						 gGraphics->LightIndexListPageSize);
	// FSLightBuffer
	gPSOBuild->AddBuffer(4, 0, eShaderType::Compute, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
						 gGraphics->LightBuffer.PageSize);

	gPSOBuild->EndPipeline();
}

void TiledForwardRenderer::DoLightCullingPass(Camera& camera)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	const Vec2u extent = gGraphics->Swapchain.Extent;

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

	push_constants.LightCount = gGraphics->LightBuffer.SlotIndex;
	push_constants.TileColumns = tile_columns;

	gPipelineCache->AddBufferOffset(0, gGraphics->GetLightGridFrameOffset());
	gPipelineCache->AddBufferOffset(0, gGraphics->GetLightIndexListFrameOffset());
	gPipelineCache->AddBufferOffset(0, gGraphics->LightBuffer.GetBaseOffset());
	gPipelineCache->Bind(ePipelineName::LightCulling, cmd);

	gGraphics->SubmitPushConstants(cmd, gPipelineCache->Request(ePipelineName::LightCulling), eShaderType::Compute,
								   push_constants);

	vkCmdDispatch(cmd.Get(), tile_columns, tile_rows, 1);

	// Make the culled light lists visible to the fragment shader
	BarrierHelper::BufferComputeToFragment(cmd, &gGraphics->LightGridBuffer);
	BarrierHelper::BufferComputeToFragment(cmd, &gGraphics->LightIndexListBuffer);
}

void TiledForwardRenderer::BindLightGridDescriptors(CommandBuffer& cmd)
{
	Pipeline& pipeline = gPipelineCache->Request(ePipelineName::Geometry);

	const uint32 buffer_offsets[] = { gGraphics->GetLightGridFrameOffset(), gGraphics->GetLightIndexListFrameOffset() };

	for (Pipeline::DescriptorRef& desc_ref : pipeline.DescriptorIDs) {
		if (desc_ref.SetIndex != scLightGridSetIndex) {
			continue;
		}

		desc_ref.pSet->Bind(scLightGridSetIndex, cmd, pipeline,
							Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
		return;
	}
}


//////////////////////////////////////////
// DeferredRenderer CompPass Functions
//////////////////////////////////////////

void TiledForwardRenderer::CreateCompositionPSO()
{
	// Create composition render stage

	CompPass.Create("Compose", gGraphics->Swapchain.Extent);

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

	gPSOBuild->SetPushConstants(eShaderType::Pixel, sizeof(CompositionPushConsts));

	// tLighting
	gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::RGBA16_Float),
								  gSamplerCache->Request(SamplerProps {}));
	// tNormal
	gPSOBuild->AddImageFromTarget(3, 0, eShaderType::Pixel, Prepass.GetTarget(eImageFormat::RGBA16_Float),
								  gSamplerCache->Request(SamplerProps {
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
								  }));

	gPSOBuild->EndPipeline();
}

void TiledForwardRenderer::RenderComposition(Camera& camera)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	gPipelineCache->Bind(ePipelineName::Composition, cmd);

	Vec2u& extent = gGraphics->Swapchain.Extent;

	CompositionPushConsts consts {
		.FrameExtent = { extent.X, extent.Y },
	};

	gGraphics->SubmitPushConstants(cmd, gPipelineCache->Request(ePipelineName::Composition), eShaderType::Pixel,
								   consts);

	// Use single triangle instead of two triangles as it removes the overlapping quads the gpu
	// renders between triangles. Source: https://wallisc.github.io/rendering/2021/04/18/Fullscreen-Pass.html
	vkCmdDraw(cmd.Get(), 3, 1, 0, 0);
}

} // namespace fx::renderer
