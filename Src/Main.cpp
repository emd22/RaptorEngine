
#define VMA_DEBUG_LOG(...) FxLogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/FxDataPack.hpp>

FX_SET_MODULE_NAME("Main")

int main()
{
    FxMemPool::GetGlobalPool().Create(50, FxUnitMebibyte);

    // char buffer_a[256];

    // FxDataPack pack {};

    // int numbers[3] = { 1, 2, 3 };
    // const std::string str = "Hello, World!";

    // pack.AddEntry(FxHashStr64("Test1"), FxMakeSlice<uint8>(reinterpret_cast<uint8*>(numbers), 3 * sizeof(int)));
    // pack.AddEntry(FxHashStr64("Test2"),
    //               FxMakeSlice<uint8>(reinterpret_cast<uint8*>(const_cast<char*>(str.data())), str.length()));

    // pack.WriteToFile("Test.fdat");

    // // FxDataPack pack {};

    // pack.ReadFromFile("Test.fdat");

    // FxSizedArray<char> data = pack.ReadSection<char>(pack.QuerySection(FxHashStr64("Test1")));
    // if (data.IsNotEmpty()) {
    //     for (char v : data) {
    //         FxLogInfo("{:02X}", v);
    //     }
    // }

    // FxSizedArray<char> data2 = pack.ReadSection<char>(pack.QuerySection(FxHashStr64("Test2")));

    // if (data2.IsNotEmpty()) {
    //     FxLogInfo("Value: {:.{}}", data2.pData, data2.Size);
    // }

    FoxtrotGame game {};
}
