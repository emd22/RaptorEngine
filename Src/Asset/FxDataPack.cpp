#include "FxDataPack.hpp"

#include <Asset/FxAssetPaths.hpp>
#include <Core/FxFile.hpp>
#include <Core/FxHash.hpp>
#include <Core/MemPool/FxMemPool.hpp>
#include <Math/FxMathUtil.hpp>

static const char* spcStrHeaderStart = "[HEADER START]";
static const char* spcStrHeaderEnd = "[HEADER END]";

static constexpr uint16 scByteAlignment = 16;
static constexpr uint8 scEntryStart[2] = { 0xFE, 0xED };

void FxDataPack::AddEntry(const FxSlice<uint8>& identifier, const FxSlice<uint8>& data)
{
    if (!Entries.IsInited()) {
        Entries.Create(16);
    }

    Entries.Insert(FxDataPackEntry {
        FxSizedArray<uint8>::CreateCopyOf(identifier.Ptr, identifier.Size),
        FxSizedArray<uint8>::CreateCopyOf(data.Ptr, data.Size),
    });
}


void FxDataPack::TextReadHeader(FxFile& file)
{
    char* line = nullptr;
    size_t line_size = 0;

    // Skip header start
    getline(&line, &line_size, file.pFileHandle);

    char temp[128];
    char ch;

    // The current index in the line.
    int line_index = 0;

    auto GetNextField = [&]()
    {
        // Index for the temp buffer
        int temp_index = 0;

        while ((ch = line[line_index])) {
            if (ch == ',' || ch == 0) {
                break;
            }

            line_index++;
            temp[temp_index++] = ch;
        }

        temp[temp_index] = 0;

        return std::atoi(temp);
    };

    while (1) {
        line_index = 0;

        getline(&line, &line_size, file.pFileHandle);

        // Check for end of header
        if (line[0] == '[') {
            break;
        }

        FxHash hash = GetNextField();
        line_index++; // Skip comma
        uint32 data_offset = GetNextField();
        line_index++;
        uint32 data_size = GetNextField();

        FxLogInfo("Hash: {}, Offset: {}, Size: {}", hash, data_offset, data_size);

        Entries.Insert(
            FxDataPackEntry { FxSizedArray<uint8>::CreateEmpty(), hash, FxSizedArray<uint8>::CreateAsSize(data_size) });
    }

    while (1) {
        line_index = 0;

        getline(&line, &line_size, file.pFileHandle);

        // Check for end of header
        if (line[0] != '[') {
            break;
        }

        char temp[128];
        int temp_index = 0;

        // Skip to the hash
        while (!isnumber(ch = line[++line_index]))
            ;

        while (isnumber(ch = line[line_index])) {
            temp[temp_index++] = ch;
            ++line_index;
        }

        temp[temp_index] = 0;
        ++temp_index;

        FxHash hash = atoi(temp);

        FxDataPackEntry* found_entry = nullptr;
        for (FxDataPackEntry& entry : Entries) {
            if (entry.IdentifierHash == hash) {
                found_entry = &entry;
            }
        }

        if (found_entry == nullptr) {
            FxLogError("Could not find data pack entry data!");
            return;
        }

        uint64 file_index = file.GetPosition();
        FxLogInfo("Index: {}", file_index);

        fread(found_entry->Data.pData, 1, found_entry->Data.Size, file.pFileHandle);


        FxLogInfo("Hash: {}", hash);


        FxLogInfo("Data {}", found_entry->Data.pData[1]);

        FxLogInfo("Data({}), {:.{}}", static_cast<uint32>(found_entry->Data.Size),
                  reinterpret_cast<char*>(found_entry->Data.pData), static_cast<uint32>(found_entry->Data.Size));
    }

    // Load the data sections
}

void FxDataPack::TextWriteHeader(FxFile& file)
{
    uint32 header_size = 0;
    for (const FxDataPackEntry& entry : Entries) {
        header_size += entry.Identifier.Size;
        header_size += sizeof(uint32);
    }

    uint32 offset = header_size;

    file.Write(spcStrHeaderEnd, '\n');
    for (FxDataPackEntry& entry : Entries) {
        entry.IdentifierHash = FxHashStr(reinterpret_cast<char*>(entry.Identifier.pData), entry.Identifier.Size);

        file.Write(entry.IdentifierHash, ',', offset, ',', entry.Data.Size, '\n');

        offset += entry.Data.Size;
    }

    file.Write("[HEADER END]\n");
}


void FxDataPack::TextWriteData(FxFile& file)
{
    for (const FxDataPackEntry& entry : Entries) {
        std::string name = std::string("[ENTRY ") + std::to_string(entry.IdentifierHash) + "]\n";
        file.Write(name.c_str());
        file.Write(entry.Data.pData, entry.Data.Size);
        file.Write('\n');
    }
}

void FxDataPack::WriteToFile(const char* name)
{
    FxFile file(name, FxFile::eWrite, FxFile::eText);

    TextWriteHeader(file);
    TextWriteData(file);

    file.Close();
}


void FxDataPack::ReadFromFile(const char* name)
{
    FxFile file(name, FxFile::eRead, FxFile::eText);

    TextReadHeader(file);

    file.Close();
}


FxDataPack::~FxDataPack() {}
