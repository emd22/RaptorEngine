#pragma once

#include <Core/File.hpp>
#include <Core/Hash.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Slice.hpp>

namespace fx {

class File;

struct DataPackEntry
{
    DataPackEntry(Hash64 id, SizedArray<uint8>&& data) : Id(id), Data(std::move(data)) {};

    DataPackEntry(Hash64 id, SizedArray<uint8>&& data, uint32 data_offset, uint32 data_size)
        : Id(id), DataOffset(data_offset), DataSize(data_size), Data(std::move(data)) {};

    DataPackEntry(DataPackEntry&& other) { (*this) = std::move(other); }

    DataPackEntry& operator=(DataPackEntry&& other)
    {
        Data = std::move(other.Data);
        Id = other.Id;
        DataOffset = other.DataOffset;
        DataSize = other.DataSize;

        other.Id = HashNull64;
        other.DataSize = 0;
        other.DataOffset = 0;

        return *this;
    }


    DataPackEntry(const DataPackEntry& other) = delete;
    DataPackEntry& operator=(const DataPackEntry& other) = delete;

public:
    Hash64 Id = HashNull64;
    uint32 DataOffset = 0;
    uint32 DataSize = 0;

    SizedArray<uint8> Data;
};

class DataPack
{
    static constexpr bool sbcBinaryFile = true;

public:
    DataPack() = default;
    DataPack(const DataPack& other) = delete;

    void AddEntry(Hash64 id, const Slice<uint8>& data);

    void WriteToFile(const char* name);
    bool ReadFromFile(const char* name);

    DataPackEntry* QuerySection(Hash64 id) const;

    template <typename TDataType>
    SizedArray<TDataType> ReadSection(DataPackEntry* entry)
    {
        if (!entry) {
            LogWarning(LC_ASSET, "Cannot read section of null entry!");

            return SizedArray<TDataType>::CreateEmpty();
        }


        SizedArray<TDataType> arr;
        arr.InitSize(entry->DataSize);

        File.SeekTo(entry->DataOffset);
        File.Read(Slice<TDataType>(arr));

        return std::move(arr);
    }

    void PrintInfo() const;

    template <typename TDataType>
    void ReadSection(DataPackEntry* entry, const Slice<TDataType>& buffer)
    {
        if (!entry) {
            LogWarning(LC_ASSET, "Cannot read section of null entry!");

            return;
        }

        uint32 read_size = std::min(entry->DataSize, buffer.Size);
        LogDebug("(EntSize={}, BufSize={}) => {}", entry->DataSize, buffer.Size, read_size);

        File.SeekTo(entry->DataOffset);
        File.Read(Slice<TDataType>(buffer.pData, read_size));
    }

    void ReadAllEntries() { BinaryReadAllData(); }

    void Close();
    bool IsOpen() const { return File.IsFileOpen(); };

    ~DataPack();

private:
    void BinaryWriteHeader();
    void BinaryWriteData();
    bool BinaryReadHeader();
    void BinaryReadAllData();

    void JumpToEntry(Hash64 id);

public:
    File File;

    PagedArray<DataPackEntry> Entries;
};

/*
 * [EntryStart] [IdSize] [Id] [Location]
 * [EntryStart] ...
 *
 * [Data...]
 */

} // namespace fx
