#pragma once

#include <Core/Types.hpp>

namespace fx::renderer {

enum class eShaderName : uint32
{
    Geometry,    // 0
    Lighting,    // 1
    Composition, // 2
    Shadows,     // 3
    Unlit,       // 4
    Text,        // 5

    NumShaders,
};

namespace ShaderNameUtil {

#define SN_READABLE_NAME(name_)                                                                                        \
    case eShaderName::name_:                                                                                           \
        return #name_

constexpr uint32 scNumShaders = static_cast<uint32>(eShaderName::NumShaders);

constexpr const char* GetName(const eShaderName id)
{
    switch (id) {
        SN_READABLE_NAME(Geometry);
        SN_READABLE_NAME(Lighting);
        SN_READABLE_NAME(Composition);
        SN_READABLE_NAME(Shadows);
        SN_READABLE_NAME(Unlit);
        SN_READABLE_NAME(Text);
    default:
        return "Unknown";
    }
    return nullptr;
}

#undef SN_READABLE_NAME

} // namespace ShaderNameUtil

} // namespace fx::renderer
