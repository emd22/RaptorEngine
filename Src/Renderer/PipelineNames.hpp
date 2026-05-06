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

#define PN_READABLE_NAME(name_)                                                                                        \
    case ePipelineName::name_:                                                                                         \
        return #name_

constexpr const char* GetName(const ePipelineName id)
{
    switch (id) {
        PN_READABLE_NAME(Geometry);
        PN_READABLE_NAME(GeometryNormalMaps);
        PN_READABLE_NAME(GeometrySkinned);
        // Lighting
        PN_READABLE_NAME(LightingOutsideVolume);
        PN_READABLE_NAME(LightingInsideVolume);
        PN_READABLE_NAME(LightingDirectional);
        PN_READABLE_NAME(Unlit);
        PN_READABLE_NAME(DebugLayer);

        PN_READABLE_NAME(TextRendering);
        PN_READABLE_NAME(Composition);

        PN_READABLE_NAME(ShadowDirectional);
    default:;
    }

    return "Unknown";
}


#undef PN_READABLE_NAME

} // namespace PipelineNameUtil


} // namespace fx::renderer
