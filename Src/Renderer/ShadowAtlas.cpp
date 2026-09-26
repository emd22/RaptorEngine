/*
 * File:        ShadowAtlas.cpp
 * Author:      emd22
 * Created:     18/09/2026
 * Description: Render shadows into a large segmented texture. Stores the directional and spotlight shadows.
 */


#include "ShadowAtlas.hpp"

#include <Asset/AssetManager.hpp>
#include <Engine.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Backend/BarrierHelper.hpp>
#include <Renderer/Backend/Sampler/SamplerCache.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/PSOBuild.hpp>
#include <Renderer/PipelineCache.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("ShadowAtlas")

/**
 * The general layout of the atlas is like this;
 * Directional on the left with the largest chunk of resolution, and the 16 spotlight slots on the right (each about
 * 512x512).
 *
 *     +-------------------+----+----+----+----+
 *     |                   |  0 |  1 |  2 |  3 |
 *     |                   +----+----+----+----+
 *     |                   |  4 |  5 |  6 |  7 |
 *     |       Sun         +----+----+----+----+
 *     |                   |  8 |  9 | 10 | 11 |
 *     |                   +----+----+----+----+
 *     |                   | 12 | 13 | 14 | 15 |
 *     +-------------------+----+----+----+----+
 *
 */

/// The atlas is sampled by the forward pass between shadow passes, and each region's render pass starts and ends in
/// this layout. It can't start from VK_IMAGE_LAYOUT_UNDEFINED, as that would throw away everything outside of the
/// render area.
static constexpr VkImageLayout scAtlasLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

static VkRect2D RegionToRect(const ShadowAtlasRegion& region)
{
	return VkRect2D {
		.offset = { .x = static_cast<int32>(region.Offset.X), .y = static_cast<int32>(region.Offset.Y) },
		.extent = { .width = region.Size.X, .height = region.Size.Y },
	};
}

ShadowAtlas::ShadowAtlas()
{
	const Vec2u size(scWidth, scHeight);

	RenderStage.Create("ShadowAtlas", size, eSizeDivisor::FullRes);

	Target atlas_target(eImageFormat::D32_Float, size, false,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
						eImageAspectFlag::Depth);

	// Every region is rendered in its own pass with the region as the render area, so the clear only touches that
	// region and the baked spot light tiles survive between frames. MoltenVK chuff going on here that I gotta figure
	// out
	atlas_target.LoadOp = eLoadOp::Clear;
	atlas_target.InitialLayout = scAtlasLayout;
	atlas_target.FinalLayout = scAtlasLayout;

	atlas_target.StencilLoadOp = eLoadOp::DontCare;
	atlas_target.StencilStoreOp = eStoreOp::DontCare;

	RenderStage.AddTarget(atlas_target);
	RenderStage.BuildRenderStage();

	{
		gPSOBuild->BeginPipeline(ePipelineName::ShadowDirectional);
		gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(ShadowPushConstants));
		gPSOBuild->UseRenderStage(RenderStage);

		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetShader(eShaderName::Shadows, {});
		gPSOBuild->SetDepthCompareOp(VK_COMPARE_OP_GREATER);
		gPSOBuild->SetCullMode(eCullMode::Back);
		gPSOBuild->SetFaceOrder(eFaceOrder::Reverse);

		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());

		gPSOBuild->AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
							 gMaterialManager->MaterialPropertiesBuffer.Size);

		gPSOBuild->EndPipeline();
	}

	{
		// Same as above, but discards on the albedo alpha for alpha masked materials (leaves, fences, etc.). The set 0
		// layout has to stay identical as the shadow passes swap between the two without rebinding the region.
		gPSOBuild->BeginPipeline(ePipelineName::ShadowDirectionalMasked);
		gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(ShadowPushConstants));
		gPSOBuild->UseRenderStage(RenderStage);

		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetShader(eShaderName::Shadows, { ShaderMacro { .pcName = "ALPHA_MASK", .pcValue = "1" } });
		gPSOBuild->SetDepthCompareOp(VK_COMPARE_OP_GREATER);
		gPSOBuild->SetCullMode(eCullMode::Back);
		gPSOBuild->SetFaceOrder(eFaceOrder::Reverse);

		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0,
							 gObjectManager->GetPageSize());
		gPSOBuild->AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
							 gMaterialManager->MaterialPropertiesBuffer.Size);

		// Set 1 (Object local), laid out like the albedo only prepass pipeline so the material's descriptors fit
		gPSOBuild->AddImage(0, 1, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
							gSamplerCache->Request({}));
		gPSOBuild->AddBuffer(4, 1, eShaderType::Pixel, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
							 gGraphics->LightBuffer.PageSize);

		gPSOBuild->EndPipeline();
	}
}

void ShadowAtlas::BindPipeline(PipelineHandle pipeline)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	// The viewport was set for the region when it began, and binding a pipeline leaves it alone
	gPipelineCache->Bind(pipeline, cmd);
}

Target* ShadowAtlas::GetTarget() { return RenderStage.GetTarget(eImageFormat::D32_Float); }

void ShadowAtlas::BeginRegion(const ShadowAtlasRegion& region)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	Target* target = GetTarget();
	Assert(target != nullptr);

	// The image only reaches the sampled layout after its first pass
	if (target->Image.ImageLayout != scAtlasLayout) {
		BarrierHelper::ImageLayoutTransition(&target->Image, scAtlasLayout, cmd, 0, 1);
	}

	const VkRect2D rect = RegionToRect(region);

	// The render pass clears its render area. The first pass clears the whole atlas, since the image starts out
	// undefined.
	//
	// This used to be one pass per frame with vkCmdClearAttachments per region, but on MoltenVK a clear in the middle
	// of a pass (after other draws) stops the draws that follow it from writing anything.
	VkRect2D render_area = rect;

	if (mbNeedsClear) {
		render_area = RegionToRect(ShadowAtlasRegion { .Offset = Vec2u::sZero, .Size = Vec2u(scWidth, scHeight) });
		mbNeedsClear = false;
	}

	// Only the region is drawn to, even when the whole atlas is being cleared
	RenderStage.Begin(cmd, render_area, rect);
	mbInitialized = true;

	gPipelineCache->AddBufferOffset(0, gObjectManager->GetBaseOffset());
	gPipelineCache->AddBufferOffset(0, 0);

	BindPipeline(gPipelineCache->FindVariant(ePipelinePass::Shadow, ePipelineFeatures::None));
}

void ShadowAtlas::EndRegion()
{
	RenderStage.End();
}

ShadowAtlasRegion ShadowAtlas::GetDirectionalRegion() const
{
	return ShadowAtlasRegion { .Offset = Vec2u::sZero, .Size = Vec2u(scDirectionalSize, scDirectionalSize) };
}

ShadowAtlasRegion ShadowAtlas::GetSpotTileRegion(ShadowTileIndex tile) const
{
	Assert(tile < scMaxSpotTiles);

	const uint32 column = tile % scSpotTilesPerRow;
	const uint32 row = tile / scSpotTilesPerRow;

	return ShadowAtlasRegion {
		.Offset = Vec2u(scDirectionalSize + (column * scSpotTileSize), row * scSpotTileSize),
		.Size = Vec2u(scSpotTileSize, scSpotTileSize),
	};
}

void ShadowAtlas::GetRegionUVTransform(const ShadowAtlasRegion& region, float32 out_transform[4]) const
{
	constexpr float32 cWidth = static_cast<float32>(scWidth);
	constexpr float32 cHeight = static_cast<float32>(scHeight);

	out_transform[0] = static_cast<float32>(region.Size.X) / cWidth;
	out_transform[1] = static_cast<float32>(region.Size.Y) / cHeight;
	out_transform[2] = static_cast<float32>(region.Offset.X) / cWidth;
	out_transform[3] = static_cast<float32>(region.Offset.Y) / cHeight;
}

ShadowTileIndex ShadowAtlas::AllocateSpotTile()
{
	for (ShadowTileIndex tile = 0; tile < scMaxSpotTiles; tile++) {
		const uint32 bit = (1U << tile);

		if ((mSpotTilesInUse & bit) == 0) {
			mSpotTilesInUse |= bit;
			return tile;
		}
	}

	return ShadowTileIndexNull;
}

void ShadowAtlas::FreeSpotTile(ShadowTileIndex tile)
{
	if (tile >= scMaxSpotTiles) {
		return;
	}

	mSpotTilesInUse &= ~(1U << tile);
}

void ShadowAtlas::Invalidate()
{
	++mGeneration;
	mbNeedsClear = true;
}

} // namespace fx::renderer
