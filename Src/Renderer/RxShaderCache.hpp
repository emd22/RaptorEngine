#pragma once

#include <Core/FxHash.hpp>
#include <Core/FxRef.hpp>
#include <Renderer/Backend/RxShader.hpp>
#include <unordered_map>

class RxShaderCache
{
public:
    RxShaderCache() = default;

    template <const char* TName>
    RxShader* Request()
    {
        constexpr FxHash64 name_hash = FxHashStr64(TName);
        FxRef<RxShader>& shader = mCache[name_hash];

        // If the shader does not exist in the cache, create a new one
        if (!shader.IsValid()) {
            shader = FxMakeRef<RxShader>(TName);
        }

        return shader.mpPtr;
    }


private:
    std::unordered_map<FxHash64, FxRef<RxShader>> mCache;
};
