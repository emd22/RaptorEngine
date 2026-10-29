#include "TiledForwardRenderer.hpp"

#include "Backend/BarrierHelper.hpp"
#include "Backend/Commands.hpp"
#include "Backend/DescriptorCache.hpp"
#include "Backend/Sampler/SamplerCache.hpp"
#include "Backend/Shader.hpp"
#include "Camera/BaseCamera.hpp"
#include "Engine.hpp"
#include "Globals.hpp"
#include "GraphicsBackend.hpp"
#include "Limits.hpp"
#include "PSOBuild.hpp"
#include "PipelineCache.hpp"
#include "ShadowAtlas.hpp"
#include "TextRenderer.hpp"

#include <Asset/AssetManager.hpp>
#include <Decal/DecalManager.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/ObjectManager.hpp>
#include <Particle/ParticleSystem.hpp>
#include <Renderer/LightProbe.hpp>
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

void TiledForwardRenderer::Create(const Vec2u& extent)
{
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10);
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 20);
	DescriptorPool.AddPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 15);
	DescriptorPool.Create(gGraphics->GetDevice(), 16);

	CreateDepthNormalPSO();
	CreateSSAOPSO();
	CreateSSAOBlurPSO();
	CreateForwardPSO();
	CreateCompositionPSO();
	CreateBitmapTextPSO();
	CreateParticlePSO();
	CreateFogPSO();
	CreateLightCullingPSO();
	CreateDebugLayerPSO();
	CreateDebugSolidPSO();
	CreateDebugOverlayPSO();


	BuildPersistentDescriptor();
}

void TiledForwardRenderer::Destroy() {}

void TiledForwardRenderer::CreateForwardPass()
{
	// Forward pass
	ForwardPass.Create("Forward", gGraphics->Swapchain.Extent, eSizeDivisor::FullRes);

	// Lit target
	ForwardPass.AddTarget(eImageFormat::RGBA16_Float, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						  eImageAspectFlag::Color);

	// Depth target. This shares the prepass's depth image and loads it rather than clearing, so the forward pass only
	// shades the fragments the prepass found visible (alpha discards included) instead of overdrawing the whole
	// scene again
	Target* prepass_depth = Prepass.GetTarget(eImageFormat::D32_Float);
	AssertMsg(prepass_depth != nullptr, "The prepass must be created before the forward pass");

	Target depth(eImageFormat::D32_Float, gGraphics->Swapchain.Extent, true,
				 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Depth);
	depth.UseImageFromTarget(prepass_depth);
	depth.LoadOp = eLoadOp::Load;
	depth.StencilLoadOp = eLoadOp::DontCare;
	depth.StencilStoreOp = eStoreOp::DontCare;
	// Where the prepass leaves it
	depth.InitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ForwardPass.AddTarget(depth);

	ForwardPass.BuildRenderStage();
}

void TiledForwardRenderer::CreateParticlePass()
{
	ParticlePass.Create("Particles", gGraphics->Swapchain.Extent, eSizeDivisor::FullRes);

	Target* lit_target = ForwardPass.GetTarget(eImageFormat::RGBA16_Float);
	AssertMsg(lit_target != nullptr, "The forward pass must be created before the particle pass");

	Target lit(eImageFormat::RGBA16_Float, gGraphics->Swapchain.Extent, true,
			   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);
	lit.UseImageFromTarget(lit_target);
	lit.LoadOp = eLoadOp::Load;
	lit.InitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ParticlePass.AddTarget(lit);

	ParticlePass.BuildRenderStage();
}

void TiledForwardRenderer::CreateDepthNormalPass()
{
	// Depth + normal prepass
	Prepass.Create("DepthNormal", gGraphics->Swapchain.Extent, eSizeDivisor::FullRes);

	// Depth target (index 0)
	Prepass.AddTarget(eImageFormat::D32_Float, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					  eImageAspectFlag::Depth);

	// World-space normals target (index 1)
	Prepass.AddTarget(eImageFormat::RGBA16_Float, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					  eImageAspectFlag::Color);

	Prepass.BuildRenderStage();
}

void TiledForwardRenderer::CreateSSAOPass()
{
	SSAOPass.Create("SSAO", gGraphics->Swapchain.Extent, eSizeDivisor::HalfRes);

	// R is the visibility term, G the packed depth edges the denoise passes read
	SSAOPass.AddTarget(eImageFormat::RG16_UNorm, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					   eImageAspectFlag::Color);

	SSAOPass.BuildRenderStage();
}

void TiledForwardRenderer::CreateSSAOBlurPass()
{
	SSAODenoisePass.Create("SSAODenoise", gGraphics->Swapchain.Extent, eSizeDivisor::HalfRes);

	SSAODenoisePass.AddTarget(eImageFormat::RG16_UNorm,
							  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
							  eImageAspectFlag::Color);

	SSAODenoisePass.BuildRenderStage();

	SSAOBlurPass.Create("SSAOBlur", gGraphics->Swapchain.Extent, eSizeDivisor::HalfRes);

	// Denoised AO output target
	SSAOBlurPass.AddTarget(eImageFormat::R8_UNorm, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
						   eImageAspectFlag::Color);

	SSAOBlurPass.BuildRenderStage();
}


void TiledForwardRenderer::BuildPersistentDescriptor()
{
	SizedArray<DescriptorEntry> ds_entries(21);

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

	// bProbeBuffer (SH irradiance, per probe)
	ds_entries.Insert(
		DescriptorEntry::AsBuffer(6, eShaderType::Pixel, &gGraphics->ProbeBuffer, 0, gGraphics->ProbePageSize));

	// bProbeVolume (spatial lookup descriptor for probe blending)
	ds_entries.Insert(DescriptorEntry::AsBuffer(7, eShaderType::Pixel, &gGraphics->ProbeVolumeBuffer, 0,
												gGraphics->ProbeVolumePageSize));

	ds_entries.Insert(DescriptorEntry::AsBuffer(13, eShaderType::Pixel, &gGraphics->ProbeGridBuffer, 0,
												gGraphics->ProbeGridPageSize));


	ds_entries.Insert(DescriptorEntry::AsImage(8, eShaderType::Pixel, gGraphics->pProbeMomentsAtlas,
											   gSamplerCache->Request({
												   eSamplerFilter::Linear,
												   eSamplerFilter::Linear,
												   eSamplerFilter::Linear,
												   eSamplerAddressMode::ClampToEdge,
											   })));

	ds_entries.Insert(DescriptorEntry::AsBuffer(14, eShaderType::Pixel, &gGraphics->ReflectionProbeBuffer, 0,
												gGraphics->ReflectionProbePageSize));

	ds_entries.Insert(DescriptorEntry::AsImage(15, eShaderType::Pixel, gGraphics->pReflectionProbes,
											   gSamplerCache->Request({
												   .AddressMode = eSamplerAddressMode::ClampToEdge,
												   .MaxLOD = static_cast<float32>(Limits::ReflectionProbeMips - 1),
											   })));

	ds_entries.Insert(DescriptorEntry::AsImage(16, eShaderType::Pixel, gGraphics->pDfgLut,
											   gSamplerCache->Request({
												   .AddressMode = eSamplerAddressMode::ClampToEdge,
											   })));

	ds_entries.Insert(DescriptorEntry::AsImage(17, eShaderType::Pixel, gGraphics->pLightmap,
											   gSamplerCache->Request({
												   eSamplerFilter::Linear,
												   eSamplerFilter::Linear,
												   eSamplerFilter::Linear,
												   eSamplerAddressMode::ClampToEdge,
											   })));

	ds_entries.Insert(DescriptorEntry::AsImage(18, eShaderType::Pixel, gGraphics->pLightmapIrradiance,
											   gSamplerCache->Request({
												   eSamplerFilter::Linear,
												   eSamplerFilter::Linear,
												   eSamplerFilter::Linear,
												   eSamplerAddressMode::ClampToEdge,
											   })));

	Target* shadow_target = gShadowAtlas->GetTarget();
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

	Target* ssao_target = SSAOBlurPass.GetTarget(eImageFormat::R8_UNorm);
	Assert(ssao_target != nullptr);

	// Linear since the AO target is half resolution so point sampling it at full resolution shows the
	// 2x2 blocks it was rendered at. Had this bad boy nearest for a while and it looked like shit
	ds_entries.Insert(DescriptorEntry::AsImage(5, eShaderType::Pixel, &ssao_target->Image,
											   gSamplerCache->Request({
												   eSamplerFilter::Linear,
												   eSamplerFilter::Linear,
												   eSamplerFilter::Linear,
											   })));

	// bDecals
	ds_entries.Insert(
		DescriptorEntry::AsBuffer(9, eShaderType::Pixel, &gGraphics->DecalBuffer, 0, gGraphics->DecalPageSize));

	// bDecalMasks
	ds_entries.Insert(DescriptorEntry::AsBuffer(10, eShaderType::Pixel, &gGraphics->DecalMaskBuffer, 0,
												gGraphics->DecalMaskPageSize));

	// tDecalAtlas and tDecalNormalAtlas, blank until DecalManager hands over the loaded atlases
	Image* null_image = gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm);
	Image* decal_atlas = (mpDecalAtlas != nullptr) ? mpDecalAtlas : null_image;
	Image* decal_normal_atlas = (mpDecalNormalAtlas != nullptr) ? mpDecalNormalAtlas : null_image;

	ds_entries.Insert(DescriptorEntry::AsImage(11, eShaderType::Pixel, decal_atlas, gSamplerCache->Request({})));
	ds_entries.Insert(DescriptorEntry::AsImage(12, eShaderType::Pixel, decal_normal_atlas, gSamplerCache->Request({})));

	result = gDescriptorCache->Request(ds_entries);
	pPersistentDescriptor = result.second;
}

void TiledForwardRenderer::RebuildPersistentDescriptor()
{
	DescriptorSet* old_descriptor = pPersistentDescriptor;

	BuildPersistentDescriptor();

	if (old_descriptor != nullptr && old_descriptor != pPersistentDescriptor) {
		gDescriptorCache->Free(old_descriptor->ID);
	}
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

		gPSOBuild->AddFrameData(1);

		gPSOBuild->EndPipeline();
	}
}

void TiledForwardRenderer::CreateSSAOBlurPSO()
{
	CreateSSAOBlurPass();

	auto BuildPipeline =
		[this](ePipelineName name, RenderStage& stage, RenderStage& source, const SizedArray<ShaderMacro>& macros)
	{
		gPSOBuild->BeginPipeline(name);

		gPSOBuild->UseRenderStage(stage);
		gPSOBuild->SetShader(eShaderName::SSAOBlur, macros);
		gPSOBuild->SetFlags(ePSOBuildFlags::NoVertices);
		gPSOBuild->SetCullMode(eCullMode::None);

		// tSSAO (visibility + packed edges)
		gPSOBuild->AddImageFromTarget(0, 0, eShaderType::Pixel, source.GetTarget(eImageFormat::RG16_UNorm),
									  gSamplerCache->Request(SamplerProps {
										  eSamplerFilter::Nearest,
										  eSamplerFilter::Nearest,
										  eSamplerFilter::Nearest,
									  }));

		gPSOBuild->AddFrameData(1);

		gPSOBuild->EndPipeline();
	};

	BuildPipeline(ePipelineName::SSAODenoise, SSAODenoisePass, SSAOPass, {});
	BuildPipeline(ePipelineName::SSAOBlur, SSAOBlurPass, SSAODenoisePass,
				  { ShaderMacro { .pcName = "FINAL_APPLY", .pcValue = "1" } });
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

void TiledForwardRenderer::CreateDebugSolidPSO()
{
	// Solid-fill variant of the debug layer (e.g. light probe markers).
	// Same shader, but triangle topology instead of line list.
	gPSOBuild->BeginPipeline(ePipelineName::DebugSolid);
	gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(DebugLayerPushConstants));

	gPSOBuild->UseRenderStage(ForwardPass);
	gPSOBuild->SetShader(eShaderName::DebugLayer, {});
	gPSOBuild->SetVertexType(eVertexType::Slim);
	gPSOBuild->SetRenderLines(false);
	gPSOBuild->SetCullMode(eCullMode::Back);
	gPSOBuild->EndPipeline();
}

void TiledForwardRenderer::CreateDebugOverlayPSO()
{
	gPSOBuild->BeginPipeline(ePipelineName::DebugOverlay);
	gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(DebugLayerPushConstants));

	gPSOBuild->UseRenderStage(ForwardPass);
	gPSOBuild->SetShader(eShaderName::DebugLayer, { ShaderMacro { .pcName = "BLENDED", .pcValue = "1" } });
	gPSOBuild->SetVertexType(eVertexType::Default);
	gPSOBuild->SetRenderLines(false);
	gPSOBuild->SetCullMode(eCullMode::Back);
	gPSOBuild->SetDepthTest(false);
	gPSOBuild->SetDepthWrite(false);

	gPSOBuild->SetTargetBlend(
		static_cast<uint32>(ForwardPass.GetTargetIndex(eImageFormat::RGBA16_Float)),
		BlendAttachment {
			.Enabled = true,
			.BlendOp = { .Ops = { .Alpha = VK_BLEND_OP_ADD, .Color = VK_BLEND_OP_ADD } },
			.AlphaBlend { .Ops { .Src = VK_BLEND_FACTOR_ZERO, .Dst = VK_BLEND_FACTOR_ONE } },
			.ColorBlend { .Ops { .Src = VK_BLEND_FACTOR_SRC_ALPHA, .Dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA } },
		});

	gPSOBuild->EndPipeline();
}

namespace {

/// Every set of features that was built up front, in the order they are drawn in. Asking for any other set makes its
/// pipeline the first time it is needed.
constexpr ePipelineFeatures scStartupFeatures[] = {
	ePipelineFeatures::None,
	ePipelineFeatures::NormalMap,
	ePipelineFeatures::Skinned,
	ePipelineFeatures::Skinned | ePipelineFeatures::NormalMap,
};

ePipelineFeatures GetGeometryShaderVariant(ePipelineFeatures features, std::vector<ShaderMacro>& out_macros,
										   eVertexType& out_vertex_type, std::string& out_suffix,
										   bool samples_lightmap = false, bool samples_vertex_colors = false)
{
	static const ShaderMacro scLightmap { .pcName = "USE_LIGHTMAP", .pcValue = "1" };
	static const ShaderMacro scVertexColor { .pcName = "USE_VERTEX_COLOR", .pcValue = "1" };

	static const ShaderMacro scNormalMaps { .pcName = "USE_NORMAL_MAPS", .pcValue = "1" };
	static const ShaderMacro scSkinning { .pcName = "USE_SKINNING", .pcValue = "1" };
	static const ShaderMacro scUnlit { .pcName = "UNLIT", .pcValue = "1" };

	const bool is_unlit = HasFlag(features, ePipelineFeatures::Unlit);

	ePipelineFeatures variant = ePipelineFeatures::None;

	if (HasFlag(features, ePipelineFeatures::Skinned)) {
		out_macros = { scNormalMaps, scSkinning };
		out_vertex_type = eVertexType::Skinned;
		out_suffix = "Skinned";
		variant = ePipelineFeatures::Skinned;
	}
	else {
		out_vertex_type = eVertexType::Default;

		if (!is_unlit && HasFlag(features, ePipelineFeatures::NormalMap)) {
			out_macros = { scNormalMaps };
			out_suffix = "NormalMaps";
			variant = ePipelineFeatures::NormalMap;
		}

		const bool has_lightmap = samples_lightmap && !is_unlit && HasFlag(features, ePipelineFeatures::Lightmap);
		const bool has_color = samples_vertex_colors && HasFlag(features, ePipelineFeatures::VertexColor);

		if (has_lightmap) {
			out_macros.push_back(scLightmap);
			out_vertex_type = eVertexType::Lightmapped;
			out_suffix += "Lightmap";
			variant |= ePipelineFeatures::Lightmap;
		}

		if (has_color) {
			out_macros.push_back(scVertexColor);
			out_vertex_type = eVertexType::Colored;

			if (has_lightmap) {
				out_vertex_type = eVertexType::LightmappedColored;
			}

			out_suffix += "VertexColor";
			variant |= ePipelineFeatures::VertexColor;
		}
	}

	if (is_unlit) {
		out_macros.push_back(scUnlit);
		out_suffix += "Unlit";
		variant |= ePipelineFeatures::Unlit;
	}

	return variant;
}

} // namespace

void TiledForwardRenderer::CreateForwardPSO()
{
	CreateForwardPass();

	for (const ePipelinePass pass :
		 { ePipelinePass::Forward, ePipelinePass::ForwardBlend, ePipelinePass::ForwardCapture }) {
		gPipelineCache->RegisterPassTemplate(pass, [this, pass](ePipelineFeatures features, PipelineDesc& out_desc)
											 { return MakeForwardDesc(pass, features, out_desc); });

		for (const ePipelineFeatures features : scStartupFeatures) {
			gPipelineCache->GetOrCreateVariant(pass, features);
		}
	}

	for (const ePipelinePass pass : { ePipelinePass::ForwardDebug, ePipelinePass::ForwardBlendDebug }) {
		gPipelineCache->RegisterPassTemplate(pass, [this, pass](ePipelineFeatures features, PipelineDesc& out_desc)
											 { return MakeForwardDesc(pass, features, out_desc); });
	}
}

void TiledForwardRenderer::CreateDepthNormalPSO()
{
	CreateDepthNormalPass();

	gPipelineCache->RegisterPassTemplate(ePipelinePass::Depth,
										 [this](ePipelineFeatures features, PipelineDesc& out_desc)
										 { return MakePrepassDesc(features, out_desc); });

	for (const ePipelineFeatures features : scStartupFeatures) {
		gPipelineCache->GetOrCreateVariant(ePipelinePass::Depth, features);
	}
}

bool TiledForwardRenderer::MakeForwardDesc(ePipelinePass pass, ePipelineFeatures features, PipelineDesc& out_desc)
{
	static const ShaderMacro scPrepassDepth { .pcName = "USE_PREPASS_DEPTH", .pcValue = "1" };
	static const ShaderMacro scProbeCapture { .pcName = "PROBE_CAPTURE", .pcValue = "1" };
	static const ShaderMacro scDebugViews { .pcName = "DEBUG_VIEWS", .pcValue = "1" };
	static const ShaderMacro scBlendPass { .pcName = "BLEND_PASS", .pcValue = "1" };

	const bool is_opaque = (pass == ePipelinePass::Forward || pass == ePipelinePass::ForwardDebug);
	const bool is_blended = (pass == ePipelinePass::ForwardBlend || pass == ePipelinePass::ForwardBlendDebug);
	const bool is_debug = (pass == ePipelinePass::ForwardDebug || pass == ePipelinePass::ForwardBlendDebug);

	// Alpha masks are handled by the shadow pass
	if (HasFlag(features, ePipelineFeatures::AlphaMask)) {
		return false;
	}

	std::string suffix;
	out_desc.Features = GetGeometryShaderVariant(features, out_desc.Macros, out_desc.VertexType, suffix, true, true);

	// Transparent geometry also discards what is nearly invisible
	if (is_blended) {
		out_desc.Macros.push_back(ShaderMacro { .pcName = "ALPHA_CUTOFF", .pcValue = "0.01" });
		out_desc.Macros.push_back(scBlendPass);
	}

	const char* pass_suffix = "";

	if (is_opaque) {
		// The prepass has just drawn this geometry and already discarded its alpha masked texels, so the depth buffer
		// holds exactly the depth of each visible fragment. Testing equal without writing depth or discarding lets the
		// GPU reject hidden fragments before shading them.
		out_desc.DepthCompareOp = VK_COMPARE_OP_EQUAL;
		out_desc.bDepthWrite = false;
		out_desc.Macros.push_back(scPrepassDepth);
	}
	else if (is_blended) {
		pass_suffix = "Transparent";
	}
	else {
		// Probe captures have no prepass, so they test and write depth themselves, and discard alpha masks here
		pass_suffix = "Capture";
		out_desc.Macros.push_back(scProbeCapture);
	}

	if (is_debug) {
		out_desc.Macros.push_back(scDebugViews);
	}

	out_desc.DebugName = std::string("Geometry") + suffix + pass_suffix + (is_debug ? "Debug" : "");

	out_desc.Shader = eShaderName::Forward;
	out_desc.pStage = &ForwardPass;
	out_desc.CullMode = eCullMode::Back;

	out_desc.PushConstantStages = eShaderType::Vertex | eShaderType::Pixel;
	out_desc.PushConstantSize = sizeof(DrawPushConstants);

	if (is_blended) {
		// Transparent surfaces are blended over what is already there, and don't write depth
		out_desc.bDepthWrite = false;

		out_desc.Blends.push_back(BlendAttachment {
			.Enabled = true,
			.BlendOp = { .Ops = { .Alpha = VK_BLEND_OP_ADD, .Color = VK_BLEND_OP_ADD } },
			.AlphaBlend { .Ops { .Src = VK_BLEND_FACTOR_ONE, .Dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA } },
			.ColorBlend { .Ops { .Src = VK_BLEND_FACTOR_SRC_ALPHA, .Dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA } },
			.TargetIndex = static_cast<uint16>(ForwardPass.GetTargetIndex(eImageFormat::RGBA16_Float)),
		});
	}

	out_desc.DeclareDescriptors = [this](PSOBuild& pso)
	{
		AddGlobalDescriptors();
		Material::DeclareDescriptors(pso);
		pso.AddFrameData(2);
	};

	return true;
}

bool TiledForwardRenderer::MakePrepassDesc(ePipelineFeatures features, PipelineDesc& out_desc)
{
	if (HasFlag(features, ePipelineFeatures::AlphaMask)) {
		return false;
	}

	std::string suffix;
	out_desc.Features = GetGeometryShaderVariant(features, out_desc.Macros, out_desc.VertexType, suffix);

	out_desc.DebugName = std::string("DepthNormal") + suffix;

	out_desc.Shader = eShaderName::DepthNormal;
	out_desc.pStage = &Prepass;
	out_desc.CullMode = eCullMode::Back;

	out_desc.PushConstantStages = eShaderType::Vertex | eShaderType::Pixel;
	out_desc.PushConstantSize = sizeof(DrawPushConstants);

	out_desc.DeclareDescriptors = [](PSOBuild& pso)
	{
		// Set 0 (Global / Per Frame)

		// bObjectBuffer
		pso.AddBuffer(0, 0, eShaderType::Vertex, &gObjectManager->mObjectGpuBuffer, 0, gObjectManager->GetPageSize());
		// bMaterialBuffer
		pso.AddBuffer(1, 0, eShaderType::Pixel, &gMaterialManager->MaterialPropertiesBuffer, 0,
					  gMaterialManager->MaterialPropertiesBuffer.Size);

		Material::DeclareDescriptors(pso);
		pso.AddFrameData(2);
	};

	return true;
}

void TiledForwardRenderer::AddGlobalDescriptors()
{
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
	// bProbeBuffer (SH irradiance, per probe)
	gPSOBuild->AddBuffer(6, 0, eShaderType::Pixel, &gGraphics->ProbeBuffer, 0, gGraphics->ProbePageSize);
	// bProbeVolume (spatial lookup descriptor for probe blending)
	gPSOBuild->AddBuffer(7, 0, eShaderType::Pixel, &gGraphics->ProbeVolumeBuffer, 0, gGraphics->ProbeVolumePageSize);
	gPSOBuild->AddBuffer(13, 0, eShaderType::Pixel, &gGraphics->ProbeGridBuffer, 0, gGraphics->ProbeGridPageSize);
	// tProbeMoments (per-probe depth moments for visibility)
	gPSOBuild->AddImage(8, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RG16_UNorm),
						gSamplerCache->Request({}));
	gPSOBuild->AddBuffer(14, 0, eShaderType::Pixel, &gGraphics->ReflectionProbeBuffer, 0,
						 gGraphics->ReflectionProbePageSize);
	gPSOBuild->AddImage(15, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RG16_UNorm),
						gSamplerCache->Request({}));
	gPSOBuild->AddImage(16, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RG16_UNorm),
						gSamplerCache->Request({}));
	gPSOBuild->AddImage(17, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RG16_UNorm),
						gSamplerCache->Request({}));
	gPSOBuild->AddImage(18, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RG16_UNorm),
						gSamplerCache->Request({}));
	// bDecals, bDecalMasks, tDecalAtlas, tDecalNormalAtlas
	AddDecalDescriptors();
	// tShadowAtlas
	gPSOBuild->AddImage(4, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::D32_Float),
						gSamplerCache->Request({}));
	// tSSAO
	gPSOBuild->AddImageFromTarget(5, 0, eShaderType::Pixel, SSAOBlurPass.GetTarget(eImageFormat::R8_UNorm),
								  gSamplerCache->Request({
									  .MinFilter = eSamplerFilter::Linear,
									  .MagFilter = eSamplerFilter::Linear,
									  .MipFilter = eSamplerFilter::Linear,
								  }));
}

void TiledForwardRenderer::AddDecalDescriptors()
{
	// bDecals
	gPSOBuild->AddBuffer(9, 0, eShaderType::Pixel, &gGraphics->DecalBuffer, 0, gGraphics->DecalPageSize);
	// bDecalMasks
	gPSOBuild->AddBuffer(10, 0, eShaderType::Pixel, &gGraphics->DecalMaskBuffer, 0, gGraphics->DecalMaskPageSize);
	// tDecalAtlas and tDecalNormalAtlas, the real atlases are only bound in the persistent descriptor set
	gPSOBuild->AddImage(11, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
						gSamplerCache->Request({}));
	gPSOBuild->AddImage(12, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
						gSamplerCache->Request({}));
}

void TiledForwardRenderer::SetDecalAtlases(Image* atlas, Image* normal_atlas)
{
	if (atlas == mpDecalAtlas && normal_atlas == mpDecalNormalAtlas) {
		return;
	}

	mpDecalAtlas = atlas;
	mpDecalNormalAtlas = normal_atlas;

	// The old set may still be in use by a frame in flight, so this builds a new one rather than updating it
	BuildPersistentDescriptor();
}

void TiledForwardRenderer::CreateLightCullingPSO()
{
	gPSOBuild->BeginPipeline(ePipelineName::LightCulling);
	gPSOBuild->SetShader(eShaderName::LightCulling, {});

	// bLightGrid
	gPSOBuild->AddBuffer(0, 0, eShaderType::Compute, &gGraphics->LightGridBuffer, 0, gGraphics->LightGridPageSize);
	// bLightIndexList
	gPSOBuild->AddBuffer(1, 0, eShaderType::Compute, &gGraphics->LightIndexListBuffer, 0,
						 gGraphics->LightIndexListPageSize);
	// FSLightBuffer
	gPSOBuild->AddBuffer(4, 0, eShaderType::Compute, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
						 gGraphics->LightBuffer.PageSize);
	// bDecals
	gPSOBuild->AddBuffer(5, 0, eShaderType::Compute, &gGraphics->DecalBuffer, 0, gGraphics->DecalPageSize);
	// bDecalMasks
	gPSOBuild->AddBuffer(6, 0, eShaderType::Compute, &gGraphics->DecalMaskBuffer, 0, gGraphics->DecalMaskPageSize);

	gPSOBuild->AddFrameData(1);

	gPSOBuild->EndPipeline();
}

void TiledForwardRenderer::DoLightCullingPass(BaseCamera& camera, const Vec2u* pExtentOverride,
											  const LightCullOverride* pLightOverride)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	const Vec2u extent = (pExtentOverride != nullptr) ? *pExtentOverride : gGraphics->Swapchain.Extent;

	const uint32 tile_columns = std::min((extent.X + (Limits::LightTileSize - 1)) / Limits::LightTileSize,
										 Limits::MaxScreenTilesX);
	const uint32 tile_rows = std::min((extent.Y + (Limits::LightTileSize - 1)) / Limits::LightTileSize,
									  Limits::MaxScreenTilesY);

	if (tile_columns * Limits::LightTileSize < extent.X || tile_rows * Limits::LightTileSize < extent.Y) {
		LogWarning(LC_RENDER, "Light grid capped at {}x{} tiles for a {}x{} view, raise Limits::MaxScreenTiles",
				   tile_columns, tile_rows, extent.X, extent.Y);
	}

	mLightTileColumns = tile_columns;
	mLightTileRows = tile_rows;

	const Mat4f& cam_matrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);

	LightCullConstants consts {};
	memcpy(consts.CameraMatrix, cam_matrix.RawData, sizeof(Mat4f));
	consts.ScreenSize[0] = static_cast<float32>(extent.X);
	consts.ScreenSize[1] = static_cast<float32>(extent.Y);

	consts.LightCount = gGraphics->LightBuffer.SlotIndex;
	consts.TileColumns = tile_columns;

	if (pLightOverride != nullptr) {
		consts.LightCount = pLightOverride->LightCount;
		consts.ReplacedLight = pLightOverride->ReplacedLight;
		consts.ReplacementSlot = pLightOverride->ReplacementSlot;
	}

	// Decals were uploaded for the player's view, and they are left out of probe captures (the only views with
	// overrides) anyway
	const bool is_main_view = (pExtentOverride == nullptr && pLightOverride == nullptr);
	consts.DecalCount = is_main_view ? gDecalManager->GetVisibleCount() : 0;

	// Probe captures cull into the same pages that the main view, and the face before, were just shaded from
	if (!is_main_view) {
		BarrierHelper::BufferFragmentToCompute(cmd, &gGraphics->LightGridBuffer);
		BarrierHelper::BufferFragmentToCompute(cmd, &gGraphics->LightIndexListBuffer);
		BarrierHelper::BufferFragmentToCompute(cmd, &gGraphics->DecalMaskBuffer);
	}

	// In binding order
	gPipelineCache->AddBufferOffset(0, gGraphics->GetLightGridFrameOffset());
	gPipelineCache->AddBufferOffset(0, gGraphics->GetLightIndexListFrameOffset());
	gPipelineCache->AddBufferOffset(0, gGraphics->LightBuffer.GetBaseOffset());
	gPipelineCache->AddBufferOffset(0, gGraphics->GetDecalFrameOffset());
	gPipelineCache->AddBufferOffset(0, gGraphics->GetDecalMaskFrameOffset());
	gPipelineCache->SetFrameData(gGraphics->FrameAlloc.Push(consts));
	gPipelineCache->Bind(ePipelineName::LightCulling, cmd);

	vkCmdDispatch(cmd.Get(), tile_columns, tile_rows, 1);

	// Make the culled light and decal lists visible to the fragment shader
	BarrierHelper::BufferComputeToFragment(cmd, &gGraphics->LightGridBuffer);
	BarrierHelper::BufferComputeToFragment(cmd, &gGraphics->LightIndexListBuffer);
	BarrierHelper::BufferComputeToFragment(cmd, &gGraphics->DecalMaskBuffer);
}

//////////////////////////////////////////
// DeferredRenderer CompPass Functions
//////////////////////////////////////////

void TiledForwardRenderer::CreateCompositionPSO()
{
	// Create composition render stage

	CompPass.Create("Compose", gGraphics->Swapchain.Extent, eSizeDivisor::FullRes);

	CompPass.MarkFinalStage();
	CompPass.BuildRenderStage();

	// Create composition pipeline

	const auto BuildPipeline = [this](ePipelineName name, const SizedArray<ShaderMacro>& macros)
	{
		gPSOBuild->BeginPipeline(name);

		gPSOBuild->UseRenderStage(CompPass);
		gPSOBuild->SetShader(eShaderName::Composition, macros);
		gPSOBuild->SetFlags(ePSOBuildFlags::NoVertices);
		gPSOBuild->SetCullMode(eCullMode::None);
		gPSOBuild->SetFaceOrder(eFaceOrder::Default);
		gPSOBuild->SetDepthTest(false);
		gPSOBuild->SetDepthWrite(false);

		// tLighting
		gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Pixel, ForwardPass.GetTarget(eImageFormat::RGBA16_Float),
									  gSamplerCache->Request(SamplerProps {}));
		gPSOBuild->AddFrameData(1);
		gPSOBuild->EndPipeline();
	};

	BuildPipeline(ePipelineName::Composition, {});
	BuildPipeline(ePipelineName::CompositionAgx, { ShaderMacro { .pcName = "USE_AGX", .pcValue = "1" } });
}

void TiledForwardRenderer::CreateBitmapTextPSO()
{
	// Ensure the font atlas and instance buffer are created before registering their descriptors.
	gTextRenderer->Create();

	const auto BuildPipeline = [this](ePipelineName name, const SizedArray<ShaderMacro>& macros)
	{
		gPSOBuild->BeginPipeline(name);
		gPSOBuild->SetPushConstants(eShaderType::Vertex, sizeof(TextPushConstants));

		gPSOBuild->UseRenderStage(ForwardPass);
		gPSOBuild->SetShader(eShaderName::BitmapText, macros);
		gPSOBuild->SetVertexType(eVertexType::Default);
		gPSOBuild->SetCullMode(eCullMode::None);
		gPSOBuild->SetDepthTest(false);
		gPSOBuild->SetDepthWrite(false);

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
		gPSOBuild->SetTargetBlend(static_cast<uint32>(ForwardPass.GetTargetIndex(eImageFormat::RGBA16_Float)), blend);

		gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gTextRenderer->GetInstanceBuffer(), 0,
							 sizeof(TextRenderer::InstanceData) * TextRenderer::scMaxGlyphs);

		gPSOBuild->AddImage(
			1, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
			gSamplerCache->Request({ eSamplerFilter::Nearest, eSamplerFilter::Nearest, eSamplerFilter::Nearest }));

		gPSOBuild->EndPipeline();
	};

	BuildPipeline(ePipelineName::TextRendering, {});
	BuildPipeline(ePipelineName::ImageRendering, { ShaderMacro { .pcName = "DRAW_IMAGE", .pcValue = "1" } });
}

void TiledForwardRenderer::CreateParticlePSO()
{
	CreateParticlePass();

	gPSOBuild->BeginPipeline(ePipelineName::Particles);

	gPSOBuild->UseRenderStage(ParticlePass);
	gPSOBuild->SetShader(eShaderName::Particle, {});
	gPSOBuild->SetFlags(ePSOBuildFlags::NoVertices);
	gPSOBuild->SetCullMode(eCullMode::None);
	gPSOBuild->SetDepthTest(false);
	gPSOBuild->SetDepthWrite(false);

	BlendAttachment blend = BlendAttachment {
		.Enabled = true,
		.BlendOp = {
			.Ops = {
				.Alpha = VK_BLEND_OP_ADD,
				.Color = VK_BLEND_OP_ADD,
			},
		},
		.AlphaBlend { .Ops { .Src = VK_BLEND_FACTOR_ZERO, .Dst = VK_BLEND_FACTOR_ONE } },
		.ColorBlend { .Ops { .Src = VK_BLEND_FACTOR_ONE, .Dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA } },
	};
	gPSOBuild->SetTargetBlend(ParticlePass.GetTargetIndex(eImageFormat::RGBA16_Float), blend);

	gPSOBuild->AddBuffer(0, 0, eShaderType::Vertex, &gParticleSystem->GetInstanceBuffer(), 0,
						 sizeof(ParticleGpuData) * ParticleSystem::scMaxParticles);

	gPSOBuild->AddImage(1, 0, eShaderType::Pixel, gAssetManager->GetNullImage(eImageFormat::RGBA8_UNorm),
						gSamplerCache->Request(SamplerProps {}));

	gPSOBuild->AddImageFromTarget(2, 0, eShaderType::Pixel, Prepass.GetTarget(eImageFormat::D32_Float),
								  gSamplerCache->Request(SamplerProps {
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
									  eSamplerFilter::Nearest,
								  }));

	gPSOBuild->AddFrameData(1);

	gPSOBuild->EndPipeline();
}

void TiledForwardRenderer::CreateFogPasses()
{
	FogApplyPass.Create("FogApply", gGraphics->Swapchain.Extent, eSizeDivisor::FullRes);

	Target* lit_target = ForwardPass.GetTarget(eImageFormat::RGBA16_Float);
	AssertMsg(lit_target != nullptr, "The forward pass must be created before the fog passes");

	Target lit(eImageFormat::RGBA16_Float, gGraphics->Swapchain.Extent, true,
			   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, eImageAspectFlag::Color);
	lit.UseImageFromTarget(lit_target);
	lit.LoadOp = eLoadOp::Load;
	lit.InitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	FogApplyPass.AddTarget(lit);

	FogApplyPass.BuildRenderStage();
}

Sampler* TiledForwardRenderer::GetFogShadowSampler()
{
	return gSamplerCache->Request(SamplerProps {
		eSamplerFilter::Linear,
		eSamplerFilter::Linear,
		eSamplerFilter::Nearest,
		eSamplerAddressMode::ClampToEdge,
		eSamplerBorderColor::FloatWhite,
		eSamplerCompareOp::LessOrEqual,
	});
}

void TiledForwardRenderer::CreateFogPSO()
{
	CreateFogPasses();

	const VkImageUsageFlags volume_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	FroxelLighting.CreateVolume(Vec2u(scFroxelGridX, scFroxelGridY), scFroxelGridZ + 1, eImageFormat::RGBA16_Float,
								volume_usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	FroxelIntegrated.CreateVolume(Vec2u(scFroxelGridX, scFroxelGridY), scFroxelGridZ, eImageFormat::RGBA16_Float,
								  volume_usage);
	FroxelHistory.CreateVolume(Vec2u(scFroxelGridX, scFroxelGridY), scFroxelGridZ + 1, eImageFormat::RGBA16_Float,
							   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	FroxelSurface.CreateVolume(Vec2u(scFroxelGridX, scFroxelGridY), 1, eImageFormat::RGBA16_Float, volume_usage);

	Sampler* point_sampler = gSamplerCache->Request(SamplerProps {
		eSamplerFilter::Nearest,
		eSamplerFilter::Nearest,
		eSamplerFilter::Nearest,
		eSamplerAddressMode::ClampToEdge,
	});

	Sampler* volume_sampler = gSamplerCache->Request(SamplerProps {
		eSamplerFilter::Linear,
		eSamplerFilter::Linear,
		eSamplerFilter::Nearest,
		eSamplerAddressMode::ClampToEdge,
	});

	Target* depth_target = Prepass.GetTarget(eImageFormat::D32_Float);
	Target* shadow_target = gShadowAtlas->GetTarget();

	gPSOBuild->BeginPipeline(ePipelineName::FogFroxelInject);
	gPSOBuild->SetShader(eShaderName::FogInject, {});

	gPSOBuild->AddImageFromTarget(0, 0, eShaderType::Compute, depth_target, point_sampler);

	gPSOBuild->AddBuffer(1, 0, eShaderType::Compute, &gGraphics->LightBuffer.GetGpuBuffer(), 0,
						 gGraphics->LightBuffer.PageSize);
	gPSOBuild->AddBuffer(2, 0, eShaderType::Compute, &gGraphics->LightGridBuffer, 0, gGraphics->LightGridPageSize);
	gPSOBuild->AddBuffer(3, 0, eShaderType::Compute, &gGraphics->LightIndexListBuffer, 0,
						 gGraphics->LightIndexListPageSize);
	gPSOBuild->AddImageFromTarget(4, 0, eShaderType::Compute, shadow_target,
								  gSamplerCache->Request({
									  eSamplerFilter::Linear,
									  eSamplerFilter::Linear,
									  eSamplerFilter::Linear,
									  eSamplerAddressMode::ClampToBorder,
									  eSamplerBorderColor::FloatWhite,
									  eSamplerCompareOp::Greater,
								  }));
	gPSOBuild->AddBuffer(5, 0, eShaderType::Compute, &gGraphics->ProbeBuffer, 0, gGraphics->ProbePageSize);
	gPSOBuild->AddBuffer(6, 0, eShaderType::Compute, &gGraphics->ProbeVolumeBuffer, 0, gGraphics->ProbeVolumePageSize);
	gPSOBuild->AddBuffer(7, 0, eShaderType::Compute, &gGraphics->ProbeGridBuffer, 0, gGraphics->ProbeGridPageSize);
	gPSOBuild->AddStorageImage(8, 0, eShaderType::Compute, &FroxelLighting);
	gPSOBuild->AddImage(9, 0, eShaderType::Compute, gGraphics->pFogShadowAtlas, GetFogShadowSampler());
	gPSOBuild->AddBuffer(10, 0, eShaderType::Compute, &gGraphics->FogShadowPageBuffer, 0,
						 gGraphics->FogShadowPageBufferSize);
	gPSOBuild->AddImage(11, 0, eShaderType::Compute, &FroxelHistory, volume_sampler);
	gPSOBuild->AddStorageImage(12, 0, eShaderType::Compute, &FroxelSurface);

	gPSOBuild->AddFrameData(1);

	gPSOBuild->EndPipeline();

	gPSOBuild->BeginPipeline(ePipelineName::FogFroxelIntegrate);
	gPSOBuild->SetShader(eShaderName::FogIntegrate, {});

	gPSOBuild->AddImage(0, 0, eShaderType::Compute, &FroxelLighting, point_sampler);
	gPSOBuild->AddStorageImage(1, 0, eShaderType::Compute, &FroxelIntegrated);

	gPSOBuild->AddFrameData(1);

	gPSOBuild->EndPipeline();

	gPSOBuild->BeginPipeline(ePipelineName::FogFroxelApply);

	gPSOBuild->UseRenderStage(FogApplyPass);
	gPSOBuild->SetShader(eShaderName::FogFroxelApply, {});
	gPSOBuild->SetFlags(ePSOBuildFlags::NoVertices);
	gPSOBuild->SetCullMode(eCullMode::None);
	gPSOBuild->SetDepthTest(false);
	gPSOBuild->SetDepthWrite(false);

	const BlendAttachment apply_blend = BlendAttachment {
		.Enabled = true,
		.BlendOp = {
			.Ops = {
				.Alpha = VK_BLEND_OP_ADD,
				.Color = VK_BLEND_OP_ADD,
			},
		},
		.AlphaBlend { .Ops { .Src = VK_BLEND_FACTOR_ZERO, .Dst = VK_BLEND_FACTOR_ONE } },
		.ColorBlend { .Ops { .Src = VK_BLEND_FACTOR_ONE, .Dst = VK_BLEND_FACTOR_SRC_ALPHA } },
	};

	gPSOBuild->SetTargetBlend(FogApplyPass.GetTargetIndex(eImageFormat::RGBA16_Float), apply_blend);

	gPSOBuild->AddImageFromTarget(0, 0, eShaderType::Pixel, depth_target, point_sampler);
	gPSOBuild->AddImage(1, 0, eShaderType::Pixel, &FroxelIntegrated, volume_sampler);
	gPSOBuild->AddImage(2, 0, eShaderType::Pixel, &FroxelLighting, volume_sampler);
	gPSOBuild->AddImage(3, 0, eShaderType::Pixel, &FroxelSurface, volume_sampler);

	gPSOBuild->AddFrameData(1);

	gPSOBuild->EndPipeline();
}

uint32 TiledForwardRenderer::GetViewConstants(const BaseCamera& camera, eObjectLayer layer)
{
	ViewConstants consts;
	memset(&consts, 0, sizeof(ViewConstants));

	consts.TargetSize[0] = gGraphics->Swapchain.Extent.X;
	consts.TargetSize[1] = gGraphics->Swapchain.Extent.Y;
	consts.TileColumns = mLightTileColumns;
	consts.TileRows = mLightTileRows;

	// Only the probe capture faces themselves get the capture flag. Keying it off
	// IsCapturePending() would also strip probe GI and SSAO from the main view for
	// every frame of a grid bake.
	if (gProbeManager != nullptr && gProbeManager->IsCapturingFaces()) {
		consts.Flags |= eDrawFlags::ProbeCapture;

		if (gProbeManager->IsCapturingBounce()) {
			consts.Flags |= eDrawFlags::ProbeBounce;
		}

		if (gProbeManager->IsCapturingReflection()) {
			consts.Flags |= eDrawFlags::ReflectionCapture;
		}
	}

	const bool is_probe_capture = HasFlag(consts.Flags, eDrawFlags::ProbeCapture);

	consts.PreExposure = gGraphics->PreExposure;

	if (is_probe_capture) {
		consts.PreExposure = 1.0f;
	}

	if (gGraphics->bBakedSunActive && !is_probe_capture) {
		consts.Flags |= eDrawFlags::BakedSun;
	}

	if (gGraphics->bBakedIndirectActive) {
		consts.Flags |= eDrawFlags::BakedIndirect;
	}

	if (gGraphics->bOnlyRenderProbes) {
		consts.Flags |= eDrawFlags::DebugIrradiance;
	}

	if (gGraphics->bRenderProbeVisibility) {
		consts.Flags |= eDrawFlags::DebugProbeVisibility;
	}

	if (gGraphics->bDisableReflectionProbes) {
		consts.Flags |= eDrawFlags::NoReflectionProbes;
	}

	if (!is_probe_capture && gGraphics->ReflectionDebugView == 1) {
		consts.Flags |= eDrawFlags::DebugReflection;
	}

	if (!is_probe_capture && gGraphics->ReflectionDebugView == 2) {
		consts.Flags |= eDrawFlags::DebugReflectionCoverage;
	}

	// Decals are placed in the world, and the view model only lines up with the world from the camera's position
	if (layer != eObjectLayer::WorldLayer) {
		consts.Flags |= eDrawFlags::NoDecals;
	}

	memcpy(consts.ViewProjection, camera.GetCameraMatrix(layer).RawData, sizeof(Mat4f));
	memcpy(consts.EyePosition, camera.GetPosition().mData, sizeof(float32) * 3);
	consts.EyePosition[3] = 1.0f;

	CachedViewConstants& cached = mCachedViews[static_cast<uint32>(layer)];
	const uint32 generation = gGraphics->FrameAlloc.GetGeneration();

	if (cached.Generation == generation && memcmp(&cached.Consts, &consts, sizeof(ViewConstants)) == 0) {
		return cached.Offset;
	}

	cached.Consts = consts;
	cached.Offset = gGraphics->FrameAlloc.Push(consts);
	cached.Generation = generation;

	return cached.Offset;
}

void TiledForwardRenderer::RenderComposition(BaseCamera& camera)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	const ePipelineName pipeline_name = (gGraphics->Tonemapper == 1) ? ePipelineName::CompositionAgx
																	 : ePipelineName::Composition;

	Vec2u& extent = gGraphics->Swapchain.Extent;

	CompositionConsts consts {
		.FrameExtent = { extent.X, extent.Y },
	};

	gPipelineCache->SetFrameData(gGraphics->FrameAlloc.Push(consts));
	gPipelineCache->Bind(pipeline_name, cmd);

	// Use single triangle instead of two triangles as it removes the overlapping quads the gpu
	// renders between triangles. Source: https://wallisc.github.io/rendering/2021/04/18/Fullscreen-Pass.html
	vkCmdDraw(cmd.Get(), 3, 1, 0, 0);
}

} // namespace fx::renderer
