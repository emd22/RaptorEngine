#include "PipelineNames.hpp"

#include <Core/Assert.hpp>


namespace fx::renderer {

#define NAME_INFO(name_)                                                                                               \
	PipelineNameInfo { name_ }


static const PipelineNameInfo scNameInfos[] = {
	NAME_INFO("DebugLayer"),	 NAME_INFO("DebugSolid"),	  NAME_INFO("DebugOverlay"),
	NAME_INFO("LightCulling"),
	NAME_INFO("TextRendering"),	 NAME_INFO("ImageRendering"), NAME_INFO("Particles"),
	NAME_INFO("Composition"),
	NAME_INFO("CompositionAgx"), NAME_INFO("SSAO"),			  NAME_INFO("SSAODenoise"),
	NAME_INFO("SSAOBlur"),
	NAME_INFO("FogFast"),		 NAME_INFO("FogVolumetric"),  NAME_INFO("FogTemporal"),
	NAME_INFO("FogDenoise"),	 NAME_INFO("FogApply"),
	NAME_INFO("FogFroxelInject"), NAME_INFO("FogFroxelIntegrate"), NAME_INFO("FogFroxelApply"),
};

static_assert(std::size(scNameInfos) == static_cast<uint32>(ePipelineName::NumPipelines));

const PipelineNameInfo& GetPipelineNameInfo(const ePipelineName name)
{
	using IdxType = std::underlying_type<ePipelineName>::type;
	const IdxType idx = static_cast<IdxType>(name);

	// Pipelines made from a description have no name in the enum
	if (idx >= static_cast<IdxType>(ePipelineName::NumPipelines)) {
		static const PipelineNameInfo scDynamicInfo = NAME_INFO("Dynamic");
		return scDynamicInfo;
	}

	return scNameInfos[idx];
}


} // namespace fx::renderer
