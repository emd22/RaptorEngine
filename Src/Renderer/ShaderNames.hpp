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

#define ENUM_TYPE eShaderName

constexpr uint32 scNumShaders = static_cast<uint32>(eShaderName::NumShaders);

constexpr const char* GetName(const eShaderName id)
{
    switch (id) {
        FX_ENUM_CASE_NAME(Geometry);
        FX_ENUM_CASE_NAME(Lighting);
        FX_ENUM_CASE_NAME(Composition);
        FX_ENUM_CASE_NAME(Shadows);
        FX_ENUM_CASE_NAME(Unlit);
        FX_ENUM_CASE_NAME(Text);
    default:
        return "Unknown";
    }
    return nullptr;
}

#undef ENUM_TYPE

} // namespace ShaderNameUtil

} // namespace fx::renderer
