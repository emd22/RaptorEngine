#include "RxShaderCache.hpp"

#include <Core/RefUtil.hpp>

namespace fx::renderer {


RxShaderCache::RxShaderCache() { mCache.InitSize(RxShaderNameUtil::scNumShaders); }

Ref<RxShader> RxShaderCache::Request(const RxShaderName id)
{
    Ref<RxShader>& shader = mCache[id];
    if (!shader.IsValid()) {
        // Shader is not in cache, create a new one
        shader = MakeRef<RxShader>(RxShaderNameUtil::GetName(id));
    }

    return shader;
}

} // namespace fx::renderer
