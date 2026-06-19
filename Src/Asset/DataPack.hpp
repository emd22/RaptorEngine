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

    FX_FORCE_INLINE bool HasData() const { return Data.pData != nullptr; }

    DataPackEntry(const DataPackEntry& other) = delete;
    DataPackEntry& operator=(const DataPackEntry& other) = delete;

public:
    Hash64 Id = HashNull64;
    uint32 DataOffset = 0;
    uint32 DataSize = 0;

    SizedArray<uint8> Data { nullptr, 0 };
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
     * @brief Finds the entry for a given ID. Does not load data from the pack into the entry.
     */
    DataPackEntry* GetEntryFast(Hash64 id) const;

    /**
     * @brief Finds the entry for a given ID.
     * @param require_data If true, the entry would be populated with data from the datapack. If false, this is
     * equivalent to `GetEntryFast()`.
     */
    DataPackEntry* GetEntry(Hash64 id, bool require_data);

    void ReadInto(DataPackEntry* entry);

    /**
     * @brief Reads data into the provided entry from the datapack using the offset and size from `entry`.
     * @param entry The entry with the data offset and size
     * @returns A slice pointing to the entry's data.
     */
    template <typename TDataType>
    Slice<TDataType> ReadSection(DataPackEntry* entry)
    {
        if (!entry) {
            LogWarning(LC_ASSET, "Cannot read section of null entry!");

            return Slice<TDataType>(nullptr, 0);
        }

        if (entry->Data.IsNotEmpty()) {
            // Data is already loaded, return the `TDataType` repr
            return Slice<TDataType>(static_cast<TDataType*>(entry->Data.pData), entry->Data.Size);
        }

        // The data is not already loaded into the entry, load it.
        entry->Data.InitSize(entry->DataSize);

        File.SeekTo(entry->DataOffset);
        File.Read(Slice<uint8>(entry->Data));

        return Slice<TDataType>(static_cast<TDataType*>(entry->Data.pData), entry->Data.Size);
    }

    void PrintInfo() const;

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
