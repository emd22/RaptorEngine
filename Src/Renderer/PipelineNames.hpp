#pragma once

#include <Core/Types.hpp>

namespace fx {

enum class ePipelineNameFlags
{
	None = 0,
	AlbedoOnly = (1 << 0),
};

FxEnumFlags(ePipelineNameFlags);
} // namespace fx


namespace fx::renderer {

enum class ePipelineName : uint16
{
	Geometry,
	GeometryNormalMaps,
	GeometrySkinned,

	// Depth+normal prepass pipelines
	DepthNormal,
	DepthNormalNormalMaps,
	DepthNormalSkinned,

	DebugLayer,

	/**
	 * @brief Forward+ compute pass that culls lights into 2D screen space tiles
	 */
	LightCulling,

	TextRendering,
	Composition,

	ShadowDirectional,

	SSAO,

	NumPipelines
};

constexpr uint32 scNumPipelines = static_cast<uint32>(ePipelineName::NumPipelines);


struct PipelineNameInfo
{
	const char* pcName;
	ePipelineNameFlags Flags;
};

const PipelineNameInfo& GetPipelineNameInfo(const ePipelineName name);


namespace PipelineNameUtil {

FX_FORCE_INLINE const char* GetName(const ePipelineName id) { return GetPipelineNameInfo(id).pcName; }


} // namespace PipelineNameUtil


} // namespace fx::renderer
