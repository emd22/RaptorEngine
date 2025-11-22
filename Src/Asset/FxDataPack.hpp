#pragma once

#include <Core/FxFile.hpp>
#include <Core/FxHash.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxSlice.hpp>

class FxFile;

struct FxDataPackEntry
{
    FxDataPackEntry(FxHash64 id, FxSizedArray<uint8>&& data) : Id(id), Data(std::move(data)) {};

    FxDataPackEntry(FxHash64 id, FxSizedArray<uint8>&& data, uint32 data_offset, uint32 data_size)
        : Id(id), DataOffset(data_offset), DataSize(data_size), Data(std::move(data)) {};

    FxDataPackEntry(FxDataPackEntry&& other)
    {
        Data = std::move(other.Data);
        Id = other.Id;
    }

    FxHash64 Id = 0;
    uint32 DataOffset = 0;
    uint32 DataSize = 0;

    FxSizedArray<uint8> Data;
};

class FxDataPack
{
    static constexpr bool sbcBinaryFile = true;

public:
    FxDataPack() = default;
    FxDataPack(const FxDataPack& other) = delete;


    // void AddEntry(const FxSlice<uint8>& identifier, const FxSlice<const uint8>& data);
    void AddEntry(FxHash64 id, const FxSlice<const uint8>& data);

    void WriteToFile(const char* name);
    bool ReadFromFile(const char* name);

    FxDataPackEntry* QuerySection(FxHash64 id) const;

    template <typename TDataType>
    FxSizedArray<TDataType> ReadSection(FxDataPackEntry* entry)
    {
        if (!entry) {
            FxLogWarning("Cannot read section of null entry!");

            return FxSizedArray<TDataType>::CreateEmpty();
        }


        FxSizedArray<TDataType> arr;
        arr.InitSize(entry->DataSize);

        File.SeekTo(entry->DataOffset);
        File.Read(FxSlice<TDataType>(arr));

        return std::move(arr);
    }

    template <typename TDataType>
    void ReadSection(FxDataPackEntry* entry, FxSlice<TDataType>& buffer)
    {
        if (!entry) {
            FxLogWarning("Cannot read section of null entry!");

            return;
        }

        uint32 read_size = std::min(entry->DataSize, buffer.Size);
        FxLogDebug("(EntSize={}, BufSize={}) => {}", entry->DataSize, buffer.Size, read_size);

        File.SeekTo(entry->DataOffset);
        File.Read(FxSlice<TDataType>(buffer.pData, read_size));
    }

    void ReadAllEntries()
    {
        if (!sbcBinaryFile) {
            TextReadAllData();
        }
        else {
            BinaryReadAllData();
        }
    }

    ~FxDataPack();

private:
    void BinaryWriteHeader();
    void BinaryWriteData();

    void BinaryReadHeader();


    void BinaryReadAllData();
    void TextReadAllData();

    void TextWriteHeader();
    void TextWriteData();

    void TextReadHeader();

    void JumpToEntry(FxHash64 id);

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
