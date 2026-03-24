
#define VMA_DEBUG_LOG(...) FxLogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/FxConfigFile.hpp>
#include <Asset/FxDataPack.hpp>
#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxDefer.hpp>
#include <Core/MemPool/FxMemPool.hpp>
#include <FxEngine.hpp>
#include <Math/FxMathConsts.hpp>
#include <Math/FxMathUtil.hpp>
#include <Renderer/RxGlobals.hpp>

FX_SET_MODULE_NAME("Main")

int main()
{
    // FxMemPool pool;
    // pool.Create(FxUnitKibibyte * 50);

    // void* a = pool.AllocRaw(1024);
    // FxLogInfo("ptr A: {:p}", a);

    // pool.FreeRaw(a);

    // void* b = pool.AllocRaw(512);
    // FxLogInfo("ptr B: {:p}", b);
    // void* c = pool.AllocRaw(512);
    // FxLogInfo("ptr C: {:p}", c);


    // FxMemPool::GetGlobalPool().Create(100, FxUnitMebibyte);

    FxEngineGlobalsInit();

    uint32* x = gEnginePool->Alloc<uint32>(2000);
    FxLogInfo("{:p}", reinterpret_cast<void*>(x));
    x = gEnginePool->Alloc<uint32>(23000);
    FxLogInfo("{:p}", reinterpret_cast<void*>(x));
    {
        FoxtrotGame game {};
    }


    FxEngineGlobalsDestroy();
    RxGlobals::Destroy();

    FxDefer(
        []()
        {
            delete gEnginePool;
            gEnginePool = nullptr;
        });
}
