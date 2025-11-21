#pragma once

#include <Core/FxFile.hpp>
#include <Core/FxHash.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxSlice.hpp>

class FxFile;

struct FxDataPackEntry
{
    FxDataPackEntry(FxHash ident_hash, FxSizedArray<uint8>&& data)
        : IdentifierHash(ident_hash), Data(std::move(data)) {};

    FxDataPackEntry(FxHash ident_hash, FxSizedArray<uint8>&& data, uint32 data_offset, uint32 data_size)
        : IdentifierHash(ident_hash), DataOffset(data_offset), DataSize(data_size), Data(std::move(data)) {};

    FxDataPackEntry(FxDataPackEntry&& other)
    {
        Data = std::move(other.Data);
        IdentifierHash = other.IdentifierHash;
    }

    FxHash IdentifierHash = 0;
    uint32 DataOffset = 0;
    uint32 DataSize = 0;


    FxSizedArray<uint8> Data;
};

class FxDataPack
{
public:
    FxDataPack() = default;
    FxDataPack(const FxDataPack& other) = delete;


    void AddEntry(const FxSlice<uint8>& identifier, const FxSlice<uint8>& data);

    void WriteToFile(const char* name);
    void ReadFromFile(const char* name);

    FxDataPackEntry* QuerySection(FxHash id) const;

    template <typename TDataType>
    FxSizedArray<TDataType> ReadSection(FxDataPackEntry* entry)
    {
        if (!entry) {
            return FxSizedArray<TDataType>::CreateEmpty();
        }

        FxSizedArray<TDataType> arr;
        arr.InitSize(entry->DataSize);

        File.SeekTo(entry->DataOffset);
        File.Read(FxSlice<TDataType>(arr));

        return std::move(arr);
    }

    ~FxDataPack();

private:
    void TextWriteHeader();
    void TextWriteData();


    void JumpToEntry(FxHash id);
    void TextReadHeader();
    void TextReadAllData();

public:
    FxFile File;

    FxPagedArray<FxDataPackEntry> Entries;
};

/*
 * [EntryStart] [IdSize] [Id] [Location]
 * [EntryStart] ...
 *
 * [Data...]
 */
