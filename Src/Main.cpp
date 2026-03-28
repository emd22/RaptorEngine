
#define VMA_DEBUG_LOG(...) FxLogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/FxConfigFile.hpp>
#include <Asset/FxDataPack.hpp>
#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxDefer.hpp>
#include <Core/FxString.hpp>
#include <Core/MemPool/FxMemPool.hpp>
#include <FxEngine.hpp>
#include <Math/FxMathConsts.hpp>
#include <Math/FxMathUtil.hpp>
#include <Renderer/RxGlobals.hpp>

FX_SET_MODULE_NAME("Main")

int main()
{
    //     FxQuat a(0.6, 0.8, 0.0, 0.0);
    //     FxQuat b(-0.8, 0.0, -0.6, 0.0);

    //     FxQuat res = a.SLerp(b, 0.1);

    //     FxLogInfo("SLERP: {}", res);

    gEnginePool = new FxMemPool;
    gEnginePool->Create(FX_MEMORY_ENGINE_POOL_SIZE);

    FxGlobals::Init();

    {
        FoxtrotGame game {};
    }

    FxGlobals::Destroy();
    RxGlobals::Destroy();

    FxDefer(
        []()
        {
            delete gEnginePool;
            gEnginePool = nullptr;
        });
}
