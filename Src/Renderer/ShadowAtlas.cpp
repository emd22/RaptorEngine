/*
 * File:        ShadowAtlas.cpp
 * Author:      emd22
 * Created:     18/09/2026
 * Description: Render shadows into a large segmented texture. Stores the directional and spotlight shadows.
 */


#include "ShadowAtlas.hpp"

#include <Asset/AssetManager.hpp>
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
 * The general layout of the atlas, which is worked out in Rust, is like this;
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

static RxShadowRegion ToRust(const ShadowAtlasRegion& region)
{
	return RxShadowRegion { .offset_x = region.Offset.X,
							.offset_y = region.Offset.Y,
							.width = region.Size.X,
							.height = region.Size.Y };
}

static ShadowAtlasRegion FromRust(const RxShadowRegion& region)
{
	return ShadowAtlasRegion { .Offset = Vec2u(region.offset_x, region.offset_y),
							   .Size = Vec2u(region.width, region.height) };
}

ShadowAtlas::~ShadowAtlas()
{
	rx_shadow_atlas_free(mpState);
	mpState = nullptr;
}

ShadowAtlas::ShadowAtlas()
{
	// The sizes are written down here for the shaders and the rest of the engine, and in Rust for the layout
	AssertEqual(rx_shadow_atlas_width(), scWidth);
	AssertEqual(rx_shadow_atlas_height(), scHeight);
	AssertEqual(rx_shadow_atlas_directional_size(), scDirectionalSize);
	AssertEqual(rx_shadow_atlas_spot_tile_size(), scSpotTileSize);
	AssertEqual(rx_shadow_atlas_max_spot_tiles(), scMaxSpotTiles);

	mpState = rx_shadow_atlas_new();

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

	gPipelineCache->RegisterPassTemplate(ePipelinePass::Shadow,
										 [this](ePipelineFeatures features, PipelineDesc& out_desc)
										 { return MakeShadowDesc(features, out_desc); });

	// Opaque casters first, then alpha masked ones
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

	static constexpr ShaderMacro scAlphaMask { .pcName = "ALPHA_MASK", .pcValue = "1" };
	static constexpr ShaderMacro scSkinning { .pcName = "USE_SKINNING", .pcValue = "1" };

	out_desc.SetFeatures(features);
	out_desc.SetDebugName(is_masked ? "ShadowDirectionalMasked"
									: (is_skinned ? "ShadowDirectionalSkinned" : "ShadowDirectional"));

	if (is_masked) {
		out_desc.SetShader(eShaderName::Shadows, std::span { &scAlphaMask, 1 });
	}
	else if (is_skinned) {
		out_desc.SetShader(eShaderName::Shadows, std::span { &scSkinning, 1 });
	}
	else {
		out_desc.SetShader(eShaderName::Shadows, {});
	}

	out_desc.UseRenderStage(RenderStage);
	out_desc.SetVertexType(is_skinned ? eVertexType::Skinned : eVertexType::Default);
	out_desc.SetDepthCompareOp(VK_COMPARE_OP_GREATER);
	out_desc.SetCullMode(eCullMode::Back);
	out_desc.SetFaceOrder(eFaceOrder::Reverse);

	out_desc.SetPushConstants(eShaderType::Vertex, sizeof(ShadowPushConstants));

	out_desc.SetDeclareDescriptors([is_masked, is_skinned](PSOBuild& pso)
	{
		// Set 0 (Global / Per Frame)
		pso.AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0, gObjectManager->GetPageSize());
		pso.AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
					  gMaterialManager->MaterialPropertiesBuffer.GetSize());

		if (is_masked || is_skinned) {
			// Set 1 (Object local), the material's own descriptors. Only the albedo is read.
			Material::DeclareDescriptors(pso);
		}
	});

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

TargetRef ShadowAtlas::GetTarget() { return RenderStage.GetTarget(eImageFormat::D32_Float); }

void ShadowAtlas::BeginRegion(const ShadowAtlasRegion& region)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	const TargetRef target = GetTarget();
	Assert(target.IsValid());

	// The image only reaches the sampled layout after its first pass
	Image atlas_image = target.GetImage();

	if (atlas_image.GetLayout() != scAtlasLayout) {
		BarrierHelper::ImageLayoutTransition(&atlas_image, scAtlasLayout, cmd, 0, 1);
	}

	const VkRect2D rect = RegionToRect(region);

	// The render pass clears its render area, which is the region, or all of the atlas for the first pass after it was
	// invalidated, since the image starts out undefined.
	//
	// This used to be one pass per frame with vkCmdClearAttachments per region, but on MoltenVK a clear in the middle
	// of a pass (after other draws) stops the draws that follow it from writing anything.
	const RxShadowRegion rust_region = ToRust(region);
	RxShadowRegion rust_render_area;
	rx_shadow_atlas_begin_region(mpState, &rust_region, &rust_render_area);

	const VkRect2D render_area = RegionToRect(FromRust(rust_render_area));

	// Only the region is drawn to, even when the whole atlas is being cleared
	RenderStage.Begin(cmd, render_area, rect);

	BindPipeline(gPipelineCache->GetOrCreateVariant(ePipelinePass::Shadow, ePipelineFeatures::None));
}

void ShadowAtlas::EndRegion()
{
	RenderStage.End();
}

ShadowAtlasRegion ShadowAtlas::GetDirectionalRegion() const
{
	RxShadowRegion region;
	rx_shadow_atlas_directional_region(&region);

	return FromRust(region);
}

ShadowAtlasRegion ShadowAtlas::GetSpotTileRegion(ShadowTileIndex tile) const
{
	RxShadowRegion region;

	const bool found = rx_shadow_atlas_spot_tile_region(tile, &region) != 0;
	AssertMsg(found, "There is no such spot light tile");

	return FromRust(region);
}

void ShadowAtlas::GetRegionUVTransform(const ShadowAtlasRegion& region, float32 out_transform[4]) const
{
	const RxShadowRegion rust_region = ToRust(region);

	rx_shadow_atlas_region_uv_transform(&rust_region, out_transform);
}

ShadowTileIndex ShadowAtlas::AllocateSpotTile()
{
	// Rust and C++ agree that no tile is UINT32_MAX
	return rx_shadow_atlas_allocate_spot_tile(mpState);
}

void ShadowAtlas::FreeSpotTile(ShadowTileIndex tile) { rx_shadow_atlas_free_spot_tile(mpState, tile); }

void ShadowAtlas::Invalidate() { rx_shadow_atlas_invalidate(mpState); }

} // namespace fx::renderer
