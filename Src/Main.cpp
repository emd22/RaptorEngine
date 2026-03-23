
#define VMA_DEBUG_LOG(...) FxLogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/FxConfigFile.hpp>
#include <Asset/FxDataPack.hpp>
#include <Asset/FxShaderCompiler.hpp>
#include <Core/MemPool/FxMemPool2.hpp>
#include <FxEngine.hpp>
#include <Math/FxMathConsts.hpp>
#include <Math/FxMathUtil.hpp>
#include <Renderer/RxGlobals.hpp>

FX_SET_MODULE_NAME("Main")

int main()
{
    FxMemPool2 pool;
    pool.Create(FxUnitKibibyte * 10);

    void* a = pool.Alloc(1024);
    FxLogInfo("ptr A: {:p}", a);

    pool.Free(a);

    void* b = pool.Alloc(512);
    FxLogInfo("ptr B: {:p}", b);
    void* c = pool.Alloc(512);
    FxLogInfo("ptr C: {:p}", c);


    // FxMemPool::GetGlobalPool().Create(100, FxUnitMebibyte);

    // FxDataPack dp;

    // FxShaderCompiler::Compile(FX_BASE_DIR "/Shaders/HLSL/GPass.hlsl", dp, {});

    // {
    //     FoxtrotGame game {};
    // }


    // FxEngineGlobalsDestroy();
    // RxGlobals::Destroy();
}
