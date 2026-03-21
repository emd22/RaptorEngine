
#define VMA_DEBUG_LOG(...) FxLogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/FxConfigFile.hpp>
#include <Asset/FxDataPack.hpp>
#include <Asset/FxShaderCompiler.hpp>
#include <FxEngine.hpp>
#include <Math/FxMathConsts.hpp>
#include <Math/FxMathUtil.hpp>
#include <Renderer/RxGlobals.hpp>

FX_SET_MODULE_NAME("Main")

int main()
{
    FxMemPool::GetGlobalPool().Create(100, FxUnitMebibyte);

    // FxDataPack dp;

    // FxShaderCompiler::Compile(FX_BASE_DIR "/Shaders/HLSL/GPass.hlsl", dp, {});

    {
        FoxtrotGame game {};
    }


    FxEngineGlobalsDestroy();
    RxGlobals::Destroy();
}
