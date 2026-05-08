#include "DataPack.hpp"

#include <Asset/AxPaths.hpp>
#include <Core/File.hpp>
#include <Core/Hash.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Math/MathUtil.hpp>

namespace fx {

static const uint16 scBinHeaderStart = 0xA1B2;
static const uint16 scBinHeaderEnd = 0x2B1A;

/*
 * HEADER BEGIN - A1B2
 *      ENTRY 1    (16 bytes)
 *      - ENTRY ID (8 bytes)
 *      - OFFSET   (4 bytes)
 *      - SIZE     (4 bytes)
 *      ENTRY 2    (16 bytes)
 *      ENTRY N
 * HEADER END   - 2B1A
 * ENTRY 1 DATA
 * ENTRY 2 DATA
 * ENTRY N DATA
 */


// static constexpr uint16 scByteAlignment = 16;
// static constexpr uint8 scEntryStart[2] = { 0xFE, 0xED };

// void DataPack::AddEntry(const Slice<uint8>& identifier, const Slice<uint8>& data)
// {
//     AddEntry(HashData64(MakeSlice<uint8>(identifier.pData, identifier.Size)), data);
// }

void DataPack::AddEntry(Hash64 id, const Slice<uint8>& data)
{
    if (!Entries.IsInited()) {
        Entries.Create(16);
    }

    // LogInfo("SAVE ({}), {:.{}}", static_cast<uint32>(data.Size), reinterpret_cast<const char*>(data.pData),
    //           static_cast<uint32>(data.Size));

    // Check to see if the entry exists and update it if it does
    if (!Entries.IsEmpty()) {
        DataPackEntry* found_entry = nullptr;
        for (DataPackEntry& entry : Entries) {
            if (entry.Id == id) {
                found_entry = &entry;
            }
        }

        if (found_entry) {
            found_entry->Data.Free();
            found_entry->Data.InitAsCopyOf(data.pData, data.Size);
            found_entry->DataSize = data.Size;

            LogInfo("Updating data pack entry {}", id);
            return;
        }
    }

    SizedArray<uint8> data_arr;
    data_arr.InitAsCopyOf(data.pData, data.Size);

    DataPackEntry pack { id, std::move(data_arr) };
    Entries.Insert(std::move(pack));
}


bool DataPack::BinaryReadHeader()
{
    uint16 header_start = 0;
    File.Read<uint16>(MakeSlice(&header_start, 1));

    uint16 number_of_entries;
    File.Read<uint16>(MakeSlice(&number_of_entries, 1));

    if (!Entries.IsInited()) {
        Entries.Create(number_of_entries);
    }

    if (header_start != scBinHeaderStart) {
        return false;
    }

    for (int index = 0; index < number_of_entries; index++) {
        Hash64 id;
        File.Read<uint64>(MakeSlice(&id, 1));

        uint32 offset;
        File.Read<uint32>(MakeSlice(&offset, 1));

        uint32 size;
        File.Read<uint32>(MakeSlice(&size, 1));


        SizedArray<uint8> data;
        data.InitSize(size);

        DataPackEntry entry { id, std::move(data), offset, size };
        Entries.Insert(std::move(entry));
    }

    uint16 header_end;
    File.Read<uint16>(MakeSlice(&header_end, 1));

    if (header_end != scBinHeaderEnd) {
        return false;
    }

    return true;
}

void DataPack::BinaryWriteHeader()
{
    uint32 header_size = 0;
    for (DataPackEntry& _ : Entries) {
        constexpr int cSizeOfEntry = sizeof(uint64) + sizeof(uint32) +
                                     sizeof(uint32); // identifier_hash, offset, data_size
        header_size += cSizeOfEntry;
    }

    // Header start(uint16), entry count (uint16), header end(uint16)
    header_size += 6;

    uint32 offset = header_size;

    File.Write(scBinHeaderStart);
    File.Write<uint16>(static_cast<uint16>(Entries.Size()));

    for (DataPackEntry& entry : Entries) {
        File.Write<uint64>(entry.Id);
        File.Write<uint32>(offset);
        File.Write<uint32>(entry.Data.Size);

        offset += entry.Data.Size;
    }

    File.Write(scBinHeaderEnd);
}

void DataPack::BinaryWriteData()
{
    for (const DataPackEntry& entry : Entries) {
        File.Write(entry.Data.pData, entry.Data.Size);
    }
}

void DataPack::BinaryReadAllData()
{
    if (Entries.IsEmpty()) {
        BinaryReadHeader();
    }

    LogDebug("Reading all data...\n");
    for (DataPackEntry& entry : Entries) {
        entry.Data = ReadSection<uint8>(&entry);
    }
}


void DataPack::PrintInfo() const
{
    LogInfo("");
    LogInfo("=== DataPack ===");


    for (const DataPackEntry& entry : Entries) {
        LogInfo("Entry 0x{:x} => Offset={}, Size={}", entry.Id, entry.DataOffset, entry.DataSize);
    }

    LogInfo("================");
    LogInfo("");
}

void DataPack::JumpToEntry(Hash64 id)
{
    DataPackEntry* found_entry = nullptr;
    for (DataPackEntry& entry : Entries) {
        if (entry.Id == id) {
            found_entry = &entry;
        }
    }

    if (!found_entry) {
        LogError("Could not find entry {} in DataPack", id);
        return;
    }
}

DataPackEntry* DataPack::QuerySection(Hash64 id) const
{
    // TODO: replace with ordered map query

    DataPackEntry* found_entry = nullptr;
    for (DataPackEntry& entry : Entries) {
        if (entry.Id == id) {
            found_entry = &entry;
            break;
        }
    }

    return found_entry;
}

void DataPack::WriteToFile(const char* name)
{
    if (File.IsFileOpen()) {
        File.Flush();
        File.Close();
    }

    File.Open(name, File::eModType::Write, File::eDataType::Binary);

    if (!File.IsFileOpen()) {
        LogWarning("Data pack '{}' could not be written to", name);
        return;
    }

    BinaryWriteHeader();
    BinaryWriteData();
}


bool DataPack::ReadFromFile(const char* name)
{
    if (File.IsFileOpen()) {
        File.Flush();
        File.Close();
    }
    if (!Entries.IsInited()) {
        Entries.Create(16);
    }
    if (!Entries.IsEmpty()) {
        Entries.Clear();
    }

    File.Open(name, File::eModType::Read, File::eDataType::Binary);

    if (!File.IsFileOpen()) {
        LogWarning("Could not open data pack '{}'", name);
        return false;
    }

    bool successful = BinaryReadHeader();
    if (!successful) {
        File.Close();
        // Open the file in write mode to clear it
        File.Open(name, File::eModType::Write, File::eDataType::Binary);
        File.Close();

        // Return unsuccessful
        return false;
    }

    return true;
}

void DataPack::Close()
{
    if (File.IsFileOpen()) {
        File.Close();
    }
}


DataPack::~DataPack() { Close(); }

} // namespace fx
