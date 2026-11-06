/*
 * File:        ShadowAtlas.cpp
 * Author:      emd22
 * Created:     18/09/2026
 * Description: Render shadows into a large segmented texture. Stores the directional and spotlight shadows.
 */


#include "ShadowAtlas.hpp"

#include <Asset/AssetManager.hpp>
#include <Core/BitUtil.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
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
 * Directional on the left with the largest chunk of resolution (dynamic sun shadows), and the spotlight slots on the
 * right (each about 512x512). The `Far` chunk is the coarse sun cascade used for volumetric fog.
 *
 * The spotlight tiles 10, 11, 14 and 15 are now designated as D0, D1, D2 and D3. These are dynamic spotlights that are
 * exempt from volumetric fog (for now) and have other special properties and are reserved for moving or updating
 * lights.
 *
 *     +-------------------+----+----+----+----+
 *     |                   |  0 |  1 |  2 |  3 |
 *     |                   +----+----+----+----+
 *     |                   |  4 |  5 |  6 |  7 |
 *     |       Sun         +----+----+----+----+
 *     |                   |         | D0 | D1 |
 *     |                   |   Far   +----+----+
 *     |                   |         | D2 | D3 |
 *     +-------------------+----+----+----+----+
 *
 */


/// Depth bias for shadow casters. With both sides drawn, lit surfaces are in their own shadow map, so this keeps them
/// from shadowing themselves. The slope term does the work, a D32 float constant unit is only a few ULPs.
static constexpr float32 scDepthBiasConstant = -16.0f;
static constexpr float32 scDepthBiasSlope = -2.0f;

static VkRect2D RegionToRect(const URect& region)
{
	const Vec2u extent = region.GetExtent();

	return VkRect2D {
		.offset = { .x = static_cast<int32>(region.Min.X), .y = static_cast<int32>(region.Min.Y) },
		.extent = { .width = extent.X, .height = extent.Y },
	};
}

ShadowAtlas::ShadowAtlas()
{
	RenderStage.Create("ShadowAtlas", scAtlasSize, eSizeDivisor::FullRes);

	Target atlas_target(eImageFormat::D32_Float, scAtlasSize, false,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
						eImageAspectFlag::Depth);

	// Every region is rendered in its own pass with the region as the render area, so the clear only touches that
	// region and the baked spot light tiles survive between frames.
	atlas_target.LoadOp = eLoadOp::Clear;
	atlas_target.InitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	atlas_target.FinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	atlas_target.StencilLoadOp = eLoadOp::DontCare;
	atlas_target.StencilStoreOp = eStoreOp::DontCare;

	RenderStage.AddTarget(atlas_target);
	RenderStage.BuildRenderStage();

	gPipelineCache->RegisterPassTemplate(ePipelinePass::Shadow,
										 [this](ePipelineFeatures features, PipelineDesc& out_desc)
										 { return MakeShadowDesc(features, out_desc); });

	// Create the shadow caster variants up front: opaque, alpha masked and skinned
	gPipelineCache->GetOrCreateVariant(ePipelinePass::Shadow, ePipelineFeatures::None);
	gPipelineCache->GetOrCreateVariant(ePipelinePass::Shadow, ePipelineFeatures::AlphaMask);
	gPipelineCache->GetOrCreateVariant(ePipelinePass::Shadow, ePipelineFeatures::Skinned);
}

bool ShadowAtlas::MakeShadowDesc(ePipelineFeatures features, PipelineDesc& out_desc)
{
	if (features != ePipelineFeatures::None && features != ePipelineFeatures::AlphaMask &&
		features != ePipelineFeatures::Skinned) {
		return false;
	}

	// Alpha masked materials (leaves, fences, etc.) discard on the albedo alpha. The set 0 layout is identical for both
	// pipelines, as the shadow passes swap between them without rebinding the region.
	const bool is_masked = (features == ePipelineFeatures::AlphaMask);
	const bool is_skinned = (features == ePipelineFeatures::Skinned);

	out_desc.Features = features;
	out_desc.DebugName = is_masked ? "ShadowDirectionalMasked"
								   : (is_skinned ? "ShadowDirectionalSkinned" : "ShadowDirectional");

	out_desc.Shader = eShaderName::Shadows;

	if (is_masked) {
		out_desc.Macros = { ShaderMacro { .pcName = "ALPHA_MASK", .pcValue = "1" } };
	}
	else if (is_skinned) {
		out_desc.Macros = { ShaderMacro { .pcName = "USE_SKINNING", .pcValue = "1" } };
	}

	out_desc.pStage = &RenderStage;
	out_desc.VertexType = is_skinned ? eVertexType::Skinned : eVertexType::Default;
	out_desc.DepthCompareOp = VK_COMPARE_OP_GREATER;

	// Both sides are drawn, so the nearest surface to the light is what casts. Drawing back faces only leaked light
	// where a caster meets its receiver (wall bases), as the far face of the caster ends exactly on the receiver.
	out_desc.CullMode = eCullMode::None;

	// Reverse Z, so a negative bias pushes casters away from the light
	out_desc.DepthBiasConstant = scDepthBiasConstant;
	out_desc.DepthBiasSlope = scDepthBiasSlope;

	out_desc.PushConstantStages = eShaderType::Vertex;
	out_desc.PushConstantSize = sizeof(ShadowPushConstants);

	out_desc.DeclareDescriptors = [](PSOBuild& pso)
	{
		// Set 0 (Global / Per Frame)
		pso.AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0, gObjectManager->GetPageSize());
		pso.AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
					  gMaterialManager->MaterialPropertiesBuffer.Size);

		// Set 1 (Object local), the material's own descriptors. Only the albedo is read.
		Material::DeclareDescriptors(pso);

		pso.AddFrameData(2);
	};

	return true;
}


void ShadowAtlas::BindPipeline(PipelineHandle pipeline)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	// The viewport was set for the region when it began, and binding a pipeline leaves it alone
	Pipeline& pl = gPipelineCache->Get(pipeline);

	if (pl.IsBuilt()) {
		pl.Bind(cmd);
	}
}

Target* ShadowAtlas::GetTarget() { return RenderStage.GetTarget(eImageFormat::D32_Float); }

void ShadowAtlas::BeginRegion(const URect& region)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	Target* target = GetTarget();
	Assert(target != nullptr);

	// The image starts out undefined, so move it into the sampled layout before the first pass
	if (target->Image.ImageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		BarrierHelper::ImageLayoutTransition(&target->Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd, 0, 1);
	}

	const VkRect2D rect = RegionToRect(region);

	// The render pass clears its render area. The first pass clears the whole atlas, since the image starts out
	// undefined.
	//
	// This used to be one pass per frame with vkCmdClearAttachments per region, but on MoltenVK a clear in the middle
	// of a pass (after other draws) stops the draws that follow it from writing anything.
	VkRect2D render_area = rect;

	if (mbNeedsClear) {
		render_area = RegionToRect(URect::FromRelative(Vec2u::scZero, scAtlasSize));
		mbNeedsClear = false;
	}

	// Only the region is drawn to, even when the whole atlas is being cleared
	RenderStage.Begin(cmd, render_area, rect);
	mbInitialized = true;

	BindPipeline(gPipelineCache->GetOrCreateVariant(ePipelinePass::Shadow, ePipelineFeatures::None));
}

void ShadowAtlas::EndRegion() { RenderStage.End(); }

URect ShadowAtlas::GetDirectionalRegion() const
{
	// The "whole hog" left half of the atlas
	return URect::FromRelative(Vec2u::scZero, Vec2u(scDirectionalSize));
}

URect ShadowAtlas::GetFarDirectionalRegion() const
{
	// The bottom of the spot grid, directly right of the sun. Uses 4 spotlight tiles worth of space
	Vec2u start_point = Vec2u(scDirectionalSize, scAtlasSize.Y - scFarDirectionalSize);
	return URect::FromRelative(start_point, Vec2u(scFarDirectionalSize));
}

URect ShadowAtlas::GetDynamicSpotTileRegion() const
{
	// Directly to the right of the far cascade, the same size as it
	return GetFarDirectionalRegion().OffsetBy(Vec2u(scFarDirectionalSize, 0U));
}

URect ShadowAtlas::GetStaticSpotTileRegion() const
{
	// The top right of the atlas, with the bottom half left for the far cascade and dynamic tiles
	const Vec2u start_point = Vec2u(scDirectionalSize, 0U);
	const Vec2u size = Vec2u(scDirectionalSize, scDirectionalSize / 2U);

	return URect::FromRelative(start_point, size);
}

URect ShadowAtlas::GetSpotTileRegion(ShadowTileIndex tile_index) const
{
	constexpr uint32 scSpotMask =
		static_cast<uint32>(eShadowAtlasMask::StaticSpot) | static_cast<uint32>(eShadowAtlasMask::DynamicSpot);

	if (tile_index >= 32U || (TileBit(tile_index) & scSpotMask) == 0U) {
		LogError("Spot tile region is invalid!");
		return URect::FromRelative(Vec2u::scZero, Vec2u::scZero);
	}

	const uint32 column = tile_index % scSpotGridColumns;
	const uint32 row = tile_index / scSpotGridColumns;

	const Vec2u region_position = Vec2u(scDirectionalSize + (column * scSpotTileSize), row * scSpotTileSize);

	return URect::FromRelative(region_position, Vec2u(scSpotTileSize, scSpotTileSize));
}

ShadowTileIndex ShadowAtlas::FindFreeTile(const eShadowAtlasMask mask) const
{
	// Bits outside the mask are forced to 1, so the first zero can only be a free tile inside it
	const uint32 unavailable = mSpotTilesInUse | ~static_cast<uint32>(mask);
	const uint32 bit_index = Bit::FindFirstZero32(unavailable);

	if (bit_index == Bit::scBitNotFound) {
		return ShadowTileIndexNull;
	}

	return bit_index;
}


void ShadowAtlas::GetRegionUVTransform(const URect& region, float32 out_transform[4]) const
{
	static_assert(scAtlasSize.X > 0U && scAtlasSize.Y > 0U);

	static constexpr Vec2f scAtlasSizeFloat = Vec2f::From<uint32>(scAtlasSize);

	const FRect f_region = FRect(region);

	const Vec2f xform_xy = f_region.GetExtent() / scAtlasSizeFloat;
	const Vec2f xform_zw = f_region.Min / scAtlasSizeFloat;

	out_transform[0] = xform_xy.X;
	out_transform[1] = xform_xy.Y;
	out_transform[2] = xform_zw.X;
	out_transform[3] = xform_zw.Y;
}


ShadowTileIndex ShadowAtlas::AllocateDynamicSpotTile()
{
	ShadowTileIndex tile_index = FindFreeTile(eShadowAtlasMask::DynamicSpot);

	// We found a tile, set it as in use
	if (tile_index != ShadowTileIndexNull) {
		mSpotTilesInUse |= TileBit(tile_index);
		return tile_index;
	}


	return ShadowTileIndexNull;
}


ShadowTileIndex ShadowAtlas::AllocateStaticSpotTile()
{
	ShadowTileIndex tile_index = FindFreeTile(eShadowAtlasMask::StaticSpot);

	// We found a tile, set it as in use
	if (tile_index != ShadowTileIndexNull) {
		mSpotTilesInUse |= TileBit(tile_index);
		return tile_index;
	}


	return ShadowTileIndexNull;
}

void ShadowAtlas::FreeSpotTile(ShadowTileIndex tile_index)
{
	constexpr uint32 scSpotMask =
		static_cast<uint32>(eShadowAtlasMask::StaticSpot) | static_cast<uint32>(eShadowAtlasMask::DynamicSpot);

	if (tile_index >= 32U || (TileBit(tile_index) & scSpotMask) == 0U) {
		return;
	}

	mSpotTilesInUse &= ~TileBit(tile_index);
}

void ShadowAtlas::Invalidate()
{
	++mGeneration;
	mbNeedsClear = true;
}

} // namespace fx::renderer
