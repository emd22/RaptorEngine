#pragma once

#include <Core/File.hpp>
#include <Core/Hash.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Slice.hpp>

namespace fx {

enum class eDataPackMode
{
    Read,
    Write,
};

class File;

/**
 * @brief An entry in the datapack. By default, an entry does not contain data unless `DataPack::ReadEntry` or
 * `DataPack::ReadAllEntries` is called. The entry only contains the position and size of the data until it is read.
 */
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

    FX_FORCE_INLINE bool HasData() const { return bIsRead && Data.pData != nullptr; }

    DataPackEntry(const DataPackEntry& other) = delete;
    DataPackEntry& operator=(const DataPackEntry& other) = delete;

public:
    Hash64 Id = HashNull64;
    uint32 DataOffset = 0;
    uint32 DataSize = 0;

    SizedArray<uint8> Data;

    bool bIsRead = false;
};

class DataPack
{
    static constexpr bool sbcBinaryFile = true;

public:
    DataPack() = default;
    DataPack(const DataPack& other) = delete;
    DataPack(eDataPackMode mode, const char* path);

    void AddEntry(Hash64 id, const Slice<uint8>& data);

    void WriteToFile(const char* name);

    /**
     * @brief Initializes the datapack from a file located at `path`.
     */
    bool ReadFromFile(const char* path);

    /**
     * @brief Finds the entry for a given ID
     */
    DataPackEntry* QuerySection(Hash64 id) const;

    /**
     * @brief Reads data from the datapack using the offset and size from `entry`.
     * @param entry The entry with the data offset and size
     * @returns The buffer with the loaded data.
     */
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
        entry->bIsRead = true;

        return std::move(arr);
    }

    void PrintInfo() const;

    /**
     * @brief Reads data from the datapack using the offset and size from `entry`.
     * @param entry The entry with the data offset and size
     * @param buffer The buffer to read into
     */
    template <typename TDataType>
    void ReadSection(DataPackEntry* entry, const Slice<TDataType>& buffer)
    {
        if (!entry) {
            LogWarning(LC_ASSET, "Cannot read section of null entry!");

            return;
        }

        uint32 read_size = std::min(entry->DataSize, buffer.Size);

        File.SeekTo(entry->DataOffset);
        File.Read(Slice<TDataType>(buffer.pData, read_size));
        entry->bIsRead = true;
    }

    /**
     * @brief Populates an entry with data from its offset in the datapack
     * @param entry The entry to read
     */
    void ReadEntry(DataPackEntry* entry);

    /**
     * @brief Reads data from the datapack into all entries.
     */
    void ReadAllEntries() { BinaryReadAllData(); }

    bool IsOpen() const { return File.IsFileOpen(); };

    void Close();
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

} // namespace fx
