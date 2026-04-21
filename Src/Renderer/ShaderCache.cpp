#include "ShaderCache.hpp"

#include <Core/RefUtil.hpp>

namespace fx::renderer {


ShaderCache::ShaderCache() { mCache.InitSize(ShaderNameUtil::scNumShaders); }

Ref<Shader> ShaderCache::Request(const eShaderName id)
{
    Ref<Shader>& shader = mCache[static_cast<uint32>(id)];
    if (!shader.IsValid()) {
        // Shader is not in cache, create a new one
        shader = MakeRef<Shader>(ShaderNameUtil::GetName(id));
    }

    return shader;
}

} // namespace fx::renderer
