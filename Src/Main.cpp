
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
    //     FxConfigFile config;


    //     FxConfigEntry positions = FxConfigEntry::Array("Pos", FxConfigValue::ValueType::eFloat);
    //     positions.AppendValue(10);
    //     positions.AppendValue(20);
    //     positions.AppendValue(30);

    //     FxConfigEntry meta = FxConfigEntry::Struct("Meta");
    //     meta.AddMember(FxConfigEntry("Text", "Test Project 1"));
    //     config.AddEntry(std::move(meta));

    //     FxConfigEntry objects = FxConfigEntry::Struct("Objects");

    //     FxConfigEntry object1 = FxConfigEntry::Struct("Entry1");
    //     object1.AddMember(std::move(positions));

    //     objects.AddMember(std::move(object1));

    //     config.AddEntry(std::move(objects));


    //     config.Write(FX_BASE_DIR "/Config/Write.prx");
    // }
    // config.Write(FX_BASE_DIR "/Config/Write.prx");

    // config.Load(FX_BASE_DIR "/Config/Test.prx");

    // FxPagedArray<FxConfigEntry>& entries = config.GetEntries();

    // for (const FxConfigEntry& entry : entries) {
    //     FxLogInfo("Entry ({}) = {}", entry.Name, entry.AsString());
    // }


    {
        FoxtrotGame game {};
    }

    FxEngineGlobalsDestroy();
}
