#pragma once

#include "Backend/RxShader.hpp"
#include "RxShaderId.hpp"

#include <Core/Hash.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>

namespace fx::renderer {

class RxShaderCache
{
public:
    RxShaderCache();

    Ref<RxShader> Request(const RxShaderName name);

private:
    SizedArray<Ref<RxShader>> mCache;
};

} // namespace fx::renderer
