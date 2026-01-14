
#define VMA_DEBUG_LOG(...) FxLogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/FxDataPack.hpp>
#include <FxEngine.hpp>
#include <Math/FxMathConsts.hpp>
#include <Math/FxMathUtil.hpp>

FX_SET_MODULE_NAME("Main")

int main()
{
    FxMemPool::GetGlobalPool().Create(200, FxUnitMebibyte);

    // float32 angle = FX_PI_2;

    // {
    //     float32 sine_sanity = std::sin(angle);
    //     float32 cosine_sanity = std::cos(angle);

    //     FxLogInfo("Sanity:");
    //     FxLogInfo("Sine: {}, Cosine:{}", sine_sanity, cosine_sanity);
    //     FxLogInfo("");
    // }

    // {
    //     float32 sine = 0.0f, cosine = 0.0f;

    //     FxMath::SinCos(angle, &sine, &cosine);

    //     FxLogInfo("Results:");
    //     FxLogInfo("Sine: {}, Cosine:{}", sine, cosine);
    //     FxLogInfo("");
    // }

    {
        FoxtrotGame game {};
    }

    FxEngineGlobalsDestroy();
}
