#pragma once

#include <Core/Types.hpp>

namespace fx::renderer {

/**
 * @brief The pipelines that are built once at startup and drawn with by name. Pipelines that come in variants
 * (geometry, prepass and shadow) are made from a PipelineDesc when they are first needed instead
 */
enum class ePipelineName : uint16
{
	DebugLayer,
	DebugSolid,

	/**
	 * @brief Forward+ compute pass that culls lights into 2D screen space tiles
	 */
	LightCulling,

	TextRendering,
	Composition,

	SSAO,
	SSAOBlur,

	NumPipelines
};

constexpr uint32 scNumPipelines = static_cast<uint32>(ePipelineName::NumPipelines);


struct PipelineNameInfo
{
	const char* pcName;
};

const PipelineNameInfo& GetPipelineNameInfo(const ePipelineName name);


namespace PipelineNameUtil {

FX_FORCE_INLINE const char* GetName(const ePipelineName id) { return GetPipelineNameInfo(id).pcName; }


} // namespace PipelineNameUtil


} // namespace fx::renderer
