#pragma once

#include "Backend/Shader.hpp"
#include "ShaderId.hpp"

#include <Core/Hash.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>

namespace fx::renderer {

class ShaderCache
{
public:
    ShaderCache();

    Ref<Shader> Request(const eShaderName name);

private:
    SizedArray<Ref<Shader>> mCache;
};

} // namespace fx::renderer
