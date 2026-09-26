#include "PipelineNames.hpp"

#include <Core/Assert.hpp>


namespace fx::renderer {

#define NAME_INFO(name_)                                                                                               \
	PipelineNameInfo { name_ }


static const PipelineNameInfo scNameInfos[] = {
	NAME_INFO("DebugLayer"),  NAME_INFO("DebugSolid"), NAME_INFO("LightCulling"), NAME_INFO("TextRendering"),
	NAME_INFO("Composition"), NAME_INFO("SSAO"),	   NAME_INFO("SSAOBlur"),
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
