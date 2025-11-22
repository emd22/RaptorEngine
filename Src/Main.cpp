
#define VMA_DEBUG_LOG(...) FxLogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/FxDataPack.hpp>

FX_SET_MODULE_NAME("Main")

int main()
{
    FxMemPool::GetGlobalPool().Create(50, FxUnitMebibyte);

    // {
    //     FxDataPack pack {};
    //     {
    //         std::string test_section = "Test";
    //         std::string data_section = "Test data";

    //         FxSlice<uint8> ident_slice = FxMakeSlice<uint8>(reinterpret_cast<uint8*>(test_section.data()),
    //                                                         test_section.length());
    //         FxSlice<uint8> data_slice = FxMakeSlice<uint8>(reinterpret_cast<uint8*>(data_section.data()),
    //                                                        data_section.length());

    //         pack.AddEntry(ident_slice, data_slice);
    //     }
    //     {
    //         std::string test_section = "Test2";
    //         std::string data_section = "Apple, Pear, Orange";

    //         FxSlice<uint8> ident_slice = FxMakeSlice<uint8>(reinterpret_cast<uint8*>(test_section.data()),
    //                                                         test_section.length());
    //         FxSlice<uint8> data_slice = FxMakeSlice<uint8>(reinterpret_cast<uint8*>(data_section.data()),
    //                                                        data_section.length());

    //         pack.AddEntry(ident_slice, data_slice);
    //     }
    //     pack.WriteToFile("Test.fdat");
    // }

    FxDataPack pack {};

    pack.ReadFromFile("Test.fdat");

    FxSizedArray<char> data = pack.ReadSection<char>(pack.QuerySection(FxHashStr64("Test2")));

    // FxLogInfo("Data: {}", data.Size);
    FxLogInfo("Data: {:.{}}", data.pData, data.Size);

    // FoxtrotGame game {};
}
