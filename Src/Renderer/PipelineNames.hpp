#pragma once

#include <Core/Types.hpp>

namespace fx::renderer {

enum class ePipelineName : uint16
{
	Geometry,
	GeometryNormalMaps,
	GeometrySkinned,

	LightingOutsideVolume,
	LightingInsideVolume,
	LightingDirectional,
	Unlit,
	DebugLayer,

	TextRendering,
	Composition,

	ShadowDirectional,

	NumPipelines
};

constexpr uint32 scNumPipelines = static_cast<uint32>(ePipelineName::NumPipelines);


namespace PipelineNameUtil {


#define ENUM_TYPE ePipelineName

constexpr const char* GetName(const ePipelineName id)
{
	switch (id) {
		FX_ENUM_CASE_NAME(Geometry);
		FX_ENUM_CASE_NAME(GeometryNormalMaps);
		FX_ENUM_CASE_NAME(GeometrySkinned);
		// Lighting
		FX_ENUM_CASE_NAME(LightingOutsideVolume);
		FX_ENUM_CASE_NAME(LightingInsideVolume);
		FX_ENUM_CASE_NAME(LightingDirectional);

		FX_ENUM_CASE_NAME(Unlit);
		FX_ENUM_CASE_NAME(DebugLayer);

		FX_ENUM_CASE_NAME(TextRendering);
		FX_ENUM_CASE_NAME(Composition);

		FX_ENUM_CASE_NAME(ShadowDirectional);
	default:;
	}

	return "Unknown";
}

#undef ENUM_TYPE


} // namespace PipelineNameUtil


} // namespace fx::renderer
