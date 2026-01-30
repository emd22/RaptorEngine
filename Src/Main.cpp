
#define VMA_DEBUG_LOG(...) FxLogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/FxConfigFile.hpp>
#include <Asset/FxDataPack.hpp>
#include <FxEngine.hpp>
#include <Math/FxMathConsts.hpp>
#include <Math/FxMathUtil.hpp>

FX_SET_MODULE_NAME("Main")

int main()
{
    FxMemPool::GetGlobalPool().Create(100, FxUnitMebibyte);

    // {
    //     FxConfigFile cf;
    //     cf.AddEntry<uint32>("IntTest", 20);
    //     cf.AddEntry<float32>("FloatTest", 10.5f);

    //     cf.Write("Test.conf");
    // }

    // {
    //     FxConfigFile cf;
    //     cf.Load("Test.conf");
    //     FxPagedArray<FxConfigEntry>& entries = cf.GetEntries();
    //     for (const FxConfigEntry& entry : entries) {
    //         FxLogInfo("ENTRY: {}", entry.AsString());
    //     }
    // }


    {
        FoxtrotGame game {};
    }

    FxEngineGlobalsDestroy();
}
