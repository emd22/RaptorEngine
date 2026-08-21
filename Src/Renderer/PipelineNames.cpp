#include "PipelineNames.hpp"

#include <Core/Assert.hpp>


namespace fx::renderer {

using eFlags = ePipelineNameFlags;

#define NAME_INFO(name_, flags_)                                                                                       \
	PipelineNameInfo { name_, flags_ }


static const PipelineNameInfo scNameInfos[] = {
	/* Geometry pipelines */
	NAME_INFO("Geometry", eFlags::AlbedoOnly),
	NAME_INFO("GeometryNormalMaps", eFlags::None),
	NAME_INFO("GeometrySkinned", eFlags::None),

	/* Unlit pipelines */
	NAME_INFO("Unlit", eFlags::AlbedoOnly),
	NAME_INFO("UnlitNormalMaps", eFlags::None),
	NAME_INFO("DebugLayer", eFlags::None),

	/* Old lighting pipelines */
	NAME_INFO("LightingOutsideVolume", eFlags::None),
	NAME_INFO("LightingInsideVolume", eFlags::None),
	NAME_INFO("LightingDirectional", eFlags::None),

	/* Forward+ light culling */
	NAME_INFO("LightCulling", eFlags::None),

	/* Other */
	NAME_INFO("TextRendering", eFlags::None),
	NAME_INFO("Composition", eFlags::None),
	NAME_INFO("ShadowDirectional", eFlags::None),
};

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
