#include "RxShaderCache.hpp"

RxShaderCache::RxShaderCache() { mCache.InitSize(RxShaderIdUtil::scNumShaders); }

FxRef<RxShader> RxShaderCache::Request(const RxShaderId id)
{
    FxRef<RxShader>& shader = mCache[id];
    if (!shader.IsValid()) {
        // Shader is not in cache, create a new one
        shader = FxMakeRef<RxShader>(RxShaderIdUtil::GetName(id));
    }

    return shader;
}
