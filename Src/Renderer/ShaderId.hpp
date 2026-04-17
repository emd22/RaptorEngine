#pragma once

#include <Core/Types.hpp>

namespace fx::renderer {

enum eShaderName : uint32
{
    Geometry,    // 0
    Lighting,    // 1
    Composition, // 2
    Shadows,     // 3
    Unlit,       // 4

    NumShaders,
};

namespace ShaderNameUtil {


constexpr uint32 scNumShaders = static_cast<uint32>(eNumShaders);

constexpr const char* GetName(const ShaderName id)
{
    switch (id) {
    case Geometry:
        return "Geometry";
    case Lighting:
        return "Lighting";
    case Composition:
        return "Composition";
    case Shadows:
        return "Shadows";
    case Unlit:
        return "Unlit";
    case NumShaders:
        break;
    }
    return nullptr;
}

} // namespace ShaderNameUtil

} // namespace fx::renderer
