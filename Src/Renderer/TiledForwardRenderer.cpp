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

	CreateForwardPSO();
	CreateSSAOPSO();
	CreateCompositionPSO();
	CreateLightCullingPSO();


	BuildPersistentDescriptor();
}

void TiledForwardRenderer::Destroy() {}

void TiledForwardRenderer::CreateGPass()
{
	// Forward pass
	ForwardPass.Create("Forward", gGraphics->Swapchain.Extent);

	// Lit target
	ForwardPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
						  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

	// Normals target
	ForwardPass.AddTarget(eImageFormat::RGBA16_Float, Target::scFullScreen,
						  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);

	// Depth target
	ForwardPass.AddTarget(eImageFormat::D32_Float, Target::scFullScreen,
						  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						  eImageAspectFlag::Depth);

	ForwardPass.BuildRenderStage();
}

void TiledForwardRenderer::CreateSSAOPass()
{
	SSAOPass.Create("Forward", gGraphics->Swapchain.Extent);

	// SSAO output target
	SSAOPass.AddTarget(eImageFormat::RGBA8_UNorm, Target::scFullScreen,
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

		// tDepth
		gPSOBuild->AddImageFromTarget(0, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::D32_Float),
									  gSamplerCache->Request(SamplerProps {
										  eSamplerFilter::Nearest,
										  eSamplerFilter::Nearest,
										  eSamplerFilter::Nearest,
									  }));
		// tNormal
		gPSOBuild->AddImageFromTarget(1, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::RGBA16_Float, 1),
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


void TiledForwardRenderer::CreateForwardPSO()
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
		gPSOBuild->AddBuffer(2, 0, eShaderType::Pixel, &gGraphics->LightGridBuffer, 0, gGraphics->LightGridPageSize);
		// bLightIndexList
		gPSOBuild->AddBuffer(3, 0, eShaderType::Pixel, &gGraphics->LightIndexListBuffer, 0,
							 gGraphics->LightIndexListPageSize);
		// tShadowAtlas
		gPSOBuild->AddImage(4, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::D32_Float),
							gSamplerCache->Request({}));


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


void TiledForwardRenderer::CreateLightingPipeline()
{
	{
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


		gPSOBuild->AddImageFromTarget(0, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::D32_Float),
									  &gGraphics->Swapchain.DepthSampler);
		gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::RGBA16_Float),
									  &gGraphics->Swapchain.NormalsSampler);
		gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::RGBA16_Float),
									  &gGraphics->Swapchain.NormalsSampler);

		gPSOBuild->AddImageFromTarget(3, 0, eShaderType::Pixel,
									  gShadowRenderer->RenderStage.GetTarget(eImageFormat::D32_Float),
									  &gGraphics->Swapchain.ShadowDepthSampler);

		gPSOBuild->AddBuffer(4, 0, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
							 gGraphics->LightBuffer.PageSize);

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

	gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(CompositionPushConsts));

	// tDepth
	gPSOBuild->AddImageFromTarget(1, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::D32_Float),
								  gSamplerCache->Request(SamplerProps {
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
								  }));
	// tLighting
	gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::RGBA16_Float),
								  gSamplerCache->Request(SamplerProps {}));
	// tNormal
	gPSOBuild->AddImageFromTarget(3, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::RGBA16_Float, 1),
								  gSamplerCache->Request(SamplerProps {
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
								  }));
	// tSSAO
	gPSOBuild->AddImageFromTarget(4, 0, eShaderType::Pixel, SSAOPass.GetTarget(eImageFormat::RGBA8_UNorm),
								  gSamplerCache->Request({
									  .MinFilter = eSamplerFilter::Nearest,
									  .MagFilter = eSamplerFilter::Nearest,
									  .MipFilter = eSamplerFilter::Nearest,
								  }));

	gPSOBuild->EndPipeline();
}

void TiledForwardRenderer::RenderComposition(Camera& camera)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	gPipelineCache->Bind(ePipelineName::Composition, cmd);

	float32* extent = gGraphics->Swapchain.Extent.mData;

	CompositionPushConsts consts {
		.FrameExtent = { static_cast<uint32>(extent[0]), static_cast<uint32>(extent[1]) },
	};

	gGraphics->SubmitPushConstants(cmd, gPipelineCache->Request(ePipelineName::Composition), eShaderType::Vertex,
								   consts);

	// Use single triangle instead of two triangles as it removes the overlapping quads the gpu
	// renders between triangles. Source: https://wallisc.github.io/rendering/2021/04/18/Fullscreen-Pass.html
	vkCmdDraw(cmd.Get(), 3, 1, 0, 0);
}

} // namespace fx::renderer
