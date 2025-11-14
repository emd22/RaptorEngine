#include "FxDataPack.hpp"

#include <Asset/FxAssetPaths.hpp>
#include <Core/MemPool/FxMemPool.hpp>
#include <Math/FxMathUtil.hpp>

static constexpr uint16 scByteAlignment = 16;

static constexpr uint8 scEntryStart[2] = { 0xFE, 0xED };

void FxDataPack::AddEntry(const FxSlice<uint8>& identifier, const FxSlice<uint8>& data)
{
    if (!Entries.IsInited()) {
        Entries.Create(16);
    }

    const uint32 aligned_size = FxMath::AlignValue<scByteAlignment>(data.Size);
    uint8* buffer = FxMemPool::Alloc<uint8>(aligned_size);
    memcpy(buffer, data.Ptr, data.Size);

    Entries.Insert(FxDataPackEntry { .Size = aligned_size, .pData = buffer });
}


void FxDataPack::WriteHeader() {}

void FxDataPack::WriteToFile(const char* name)
{
    // FxFile file(std::string(FxAssetPath(FxAssetPathQuery::eDataPacks)), "wb");
}


FxDataPack::~FxDataPack()
{
    for (FxDataPackEntry& marker : Entries) {
        FxMemPool::Free(marker.pData);
    }

    Entries.Clear();
}
