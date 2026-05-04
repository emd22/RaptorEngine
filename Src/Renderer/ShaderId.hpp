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


constexpr uint32 scNumShaders = static_cast<uint32>(eShaderName::NumShaders);

constexpr const char* GetName(const eShaderName id)
{
    switch (id) {
    case eShaderName::Geometry:
        return "Geometry";
    case eShaderName::Lighting:
        return "Lighting";
    case eShaderName::Composition:
        return "Composition";
    case eShaderName::Shadows:
        return "Shadows";
    case eShaderName::Unlit:
        return "Unlit";
    case eShaderName::Text:
        return "Text";
    case eShaderName::NumShaders:
        break;
    }
    return nullptr;
}

} // namespace ShaderNameUtil

} // namespace fx::renderer
