#include "PipelineNames.hpp"

#include "PipelineCache.hpp"

#include <Core/Assert.hpp>


namespace fx::renderer {

using eFlags = ePipelineNameFlags;

#define NAME_INFO(name_, flags_)                                                                                       \
	PipelineNameInfo { name_, flags_ }


static const PipelineNameInfo scNameInfos[] = {
	/* Geometry pipelines */
	NAME_INFO("Geometry", eFlags::AlbedoOnly),
	NAME_INFO("GeometryNormalMaps", eFlags::None),
	NAME_INFO("GeometrySkinned", eFlags::Skinned),

	NAME_INFO("GeometryTransparent", eFlags::AlbedoOnly),
	NAME_INFO("GeometryNormalMapsTransparent", eFlags::None),
	NAME_INFO("GeometrySkinnedTransparent", eFlags::Skinned),

	/* Depth + Normal prepass */
	NAME_INFO("DepthNormal", eFlags::AlbedoOnly),
	NAME_INFO("DepthNormalNormalMaps", eFlags::None),
	NAME_INFO("DepthNormalSkinned", eFlags::Skinned),

	NAME_INFO("DebugLayer", eFlags::None),
	NAME_INFO("DebugSolid", eFlags::None),

	/* Forward+ light culling */
	NAME_INFO("LightCulling", eFlags::None),

	/* Other */
	NAME_INFO("TextRendering", eFlags::None),
	NAME_INFO("Composition", eFlags::None),
	NAME_INFO("ShadowDirectional", eFlags::None),
	NAME_INFO("ShadowDirectionalMasked", eFlags::AlbedoOnly),

	NAME_INFO("SSAO", eFlags::None),
	NAME_INFO("SSAOBlur", eFlags::None),
};

static_assert(std::size(scNameInfos) == static_cast<uint32>(ePipelineName::NumPipelines));

void RegisterPipelineVariants(PipelineCache& cache)
{
	using enum ePipelineFeatures;

	struct Variant
	{
		ePipelinePass Pass;
		ePipelineFeatures Features;
		ePipelineName Pipeline;
	};

	// A skinned pipeline also handles normal maps, so both sets of features share it
	static const Variant scVariants[] = {
		{ ePipelinePass::Forward, None, ePipelineName::Geometry },
		{ ePipelinePass::Forward, NormalMap, ePipelineName::GeometryNormalMaps },
		{ ePipelinePass::Forward, Skinned, ePipelineName::GeometrySkinned },
		{ ePipelinePass::Forward, Skinned | NormalMap, ePipelineName::GeometrySkinned },

		{ ePipelinePass::ForwardBlend, None, ePipelineName::GeometryTransparent },
		{ ePipelinePass::ForwardBlend, NormalMap, ePipelineName::GeometryNormalMapsTransparent },
		{ ePipelinePass::ForwardBlend, Skinned, ePipelineName::GeometrySkinnedTransparent },
		{ ePipelinePass::ForwardBlend, Skinned | NormalMap, ePipelineName::GeometrySkinnedTransparent },

		{ ePipelinePass::Depth, None, ePipelineName::DepthNormal },
		{ ePipelinePass::Depth, NormalMap, ePipelineName::DepthNormalNormalMaps },
		{ ePipelinePass::Depth, Skinned, ePipelineName::DepthNormalSkinned },
		{ ePipelinePass::Depth, Skinned | NormalMap, ePipelineName::DepthNormalSkinned },

		{ ePipelinePass::Shadow, None, ePipelineName::ShadowDirectional },
		{ ePipelinePass::Shadow, AlphaMask, ePipelineName::ShadowDirectionalMasked },
	};

	for (const Variant& variant : scVariants) {
		cache.RegisterVariant(variant.Pass, variant.Features, PipelineCache::GetHandle(variant.Pipeline));
	}
}

const PipelineNameInfo& GetPipelineNameInfo(const ePipelineName name)
{
	using IdxType = std::underlying_type<ePipelineName>::type;
	const IdxType idx = static_cast<IdxType>(name);

#ifdef FX_BUILD_DEBUG
	AssertLess(idx, static_cast<IdxType>(ePipelineName::NumPipelines));
#endif

	return scNameInfos[idx];
}


} // namespace fx::renderer
