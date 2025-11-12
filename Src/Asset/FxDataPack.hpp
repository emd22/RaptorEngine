#pragma once

#include <Core/FxFile.hpp>
#include <Core/FxSlice.hpp>

struct FxDataPackEntry
{
    uint32 Size;
    uint8* pData = nullptr;
};

class FxDataPack
{
public:
    FxDataPack() = default;
    FxDataPack(const FxDataPack& other) = delete;

private:
    void AddEntry(const FxSlice<uint8>& identifier, const FxSlice<uint8>& data);

    void WriteToFile(const char* name);

    ~FxDataPack();

private:
    void WriteHeader();

public:
    FxPagedArray<FxDataPackEntry> Entries;
};

/*
 * [EntryStart] [IdSize] [Id] [Location]
 * [EntryStart] ...
 *
 * [Data...]
 */
