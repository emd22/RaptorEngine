#pragma once

#include "Backend/RxShader.hpp"
#include "RxShaderId.hpp"

#include <Core/FxHash.hpp>
#include <Core/FxRef.hpp>
#include <Core/FxSizedArray.hpp>

class RxShaderCache
{
public:
    RxShaderCache();

    FxRef<RxShader> Request(const RxShaderId id);


private:
    FxSizedArray<FxRef<RxShader>> mCache;
};
