
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

    FxConfigFile config;
    config.Load(FX_BASE_DIR "/Config/Test.prx");

    FxPagedArray<FxConfigEntry>& entries = config.GetEntries();

    for (const FxConfigEntry& entry : entries) {
        FxLogInfo("Entry ({}) = {}", entry.Name, entry.AsString());
    }


    // {
    //     FoxtrotGame game {};
    // }

    FxEngineGlobalsDestroy();
}
