#include "FxDataPack.hpp"

#include <Asset/FxAssetPaths.hpp>
#include <Core/FxFile.hpp>
#include <Core/FxHash.hpp>
#include <Core/MemPool/FxMemPool.hpp>
#include <Math/FxMathUtil.hpp>

static const char* spcStrHeaderStart = "[HEADER START]\n";
static const char* spcStrHeaderEnd = "[HEADER END]\n";

static constexpr uint16 scByteAlignment = 16;
static constexpr uint8 scEntryStart[2] = { 0xFE, 0xED };

void FxDataPack::AddEntry(const FxSlice<uint8>& identifier, const FxSlice<uint8>& data)
{
    AddEntry(FxHashData64(FxMakeSlice<uint8>(identifier.pData, identifier.Size)), data);
}

void FxDataPack::AddEntry(FxHash64 id, const FxSlice<uint8>& data)
{
    if (!Entries.IsInited()) {
        Entries.Create(16);
    }

    FxLogInfo("SAVE ({}), {:.{}}", static_cast<uint32>(data.Size), reinterpret_cast<char*>(data.pData),
              static_cast<uint32>(data.Size));

    Entries.Insert(FxDataPackEntry { id, FxSizedArray<uint8>::CreateCopyOf(data.pData, data.Size) });
}


void FxDataPack::TextReadHeader()
{
    if (!Entries.IsInited()) {
        Entries.Create(16);
    }

    char* line = nullptr;
    size_t line_size = 0;

    // Skip header start
    getline(&line, &line_size, File.pFileHandle);

    char temp[128];
    char ch;

    // The current index in the line.
    int line_index = 0;

    auto GetNextField64 = [&]()
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


        char* end = temp + temp_index;
        return static_cast<FxHash64>(std::strtoull(temp, &end, 10));
    };

    auto GetNextField32 = [&]()
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

        return static_cast<uint32>(std::atol(temp));
    };

    while (1) {
        line_index = 0;

        getline(&line, &line_size, File.pFileHandle);

        // Check for end of header
        if (line[0] == '[') {
            break;
        }

        FxHash64 hash = GetNextField64();
        line_index++; // Skip comma
        uint32 data_offset = GetNextField32();
        line_index++;
        uint32 data_size = GetNextField32();

        FxLogInfo("Hash: {}, Offset: {}, Size: {}", hash, data_offset, data_size);

        Entries.Insert(FxDataPackEntry { hash, FxSizedArray<uint8>::CreateAsSize(data_size), data_offset, data_size });
    }

    // Free the getline allocated string
    free(line);
}

void FxDataPack::TextReadAllData()
{
    char* line = nullptr;
    size_t line_size = 0;
    int line_index = 0;

    char ch;

    while (1) {
        // Read a line from the file
        getline(&line, &line_size, File.pFileHandle);

        // Skip until there are no entries.
        // Each entry begins with [ENTRY {identifier}], so we can read that and then consume the next
        // 'n' bytes of data to consume the entry.
        if (line[0] != '[') {
            break;
        }

        char hash_buffer[128];
        int hash_index = 0;

        // Skip to the hash
        while (!isnumber(ch = line[++line_index]))
            ;

        // Read the hash in
        while (isnumber(ch = line[line_index])) {
            hash_buffer[hash_index++] = ch;
            ++line_index;
        }

        hash_buffer[hash_index] = 0;
        ++hash_index;

        char* end = hash_buffer + hash_index;
        FxHash64 hash = static_cast<uint64>(static_cast<FxHash64>(std::strtoull(hash_buffer, &end, 10)));

        FxDataPackEntry* found_entry = nullptr;
        for (FxDataPackEntry& entry : Entries) {
            if (entry.Id == hash) {
                found_entry = &entry;
            }
        }

        if (found_entry == nullptr) {
            FxLogError("Could not find data pack entry data!");
            return;
        }

        uint64 file_index = File.GetPosition();
        FxLogInfo("Index: {}", file_index);

        fread(found_entry->Data.pData, 1, found_entry->Data.Size, File.pFileHandle);

        FxLogInfo("Hash: {}", hash);
    }

    // Free the getline allocated string
    free(line);
}

void FxDataPack::JumpToEntry(FxHash64 id)
{
    FxDataPackEntry* found_entry = nullptr;
    for (FxDataPackEntry& entry : Entries) {
        if (entry.Id == id) {
            found_entry = &entry;
        }
    }

    if (!found_entry) {
        FxLogError("Could not find entry {} in FxDataPack", id);
        return;
    }
}

void FxDataPack::TextWriteHeader()
{
    uint32 header_size = 0;
    for (FxDataPackEntry& _ : Entries) {
        constexpr int cSizeOfEntry = 20 + 1 + 10 + 1 + 10 + 1; // identifier_hash,offset,data_size\n
        header_size += cSizeOfEntry;
    }

    header_size += strlen(spcStrHeaderStart);
    header_size += strlen(spcStrHeaderEnd);

    uint32 offset = header_size;

    File.Write(spcStrHeaderStart);

    for (FxDataPackEntry& entry : Entries) {
        // [ENTRY 00000000000000000000]\n
        constexpr int cEntryMarkerLength = 29;

        offset += cEntryMarkerLength;

        std::string header_entry = std::format("{:020},{:010},{:010}\n", entry.Id, offset, entry.Data.Size);
        File.Write(header_entry.c_str());

        offset += entry.Data.Size + 1; // Where +1 is for the extra newline appended (in text mode)
    }

    File.Write(spcStrHeaderEnd);
}


void FxDataPack::TextWriteData()
{
    for (const FxDataPackEntry& entry : Entries) {
        std::string entry_header = std::format("[ENTRY {:020}]\n", entry.Id);
        File.Write(entry_header.c_str());
        File.Write(entry.Data.pData, entry.Data.Size);

        FxLogInfo("WRITE ({}), {:.{}}", static_cast<uint32>(entry.Data.Size), reinterpret_cast<char*>(entry.Data.pData),
                  static_cast<uint32>(entry.Data.Size));

        File.Write('\n');
    }
}

FxDataPackEntry* FxDataPack::QuerySection(FxHash64 id) const
{
    // TODO: replace with ordered map query

    FxDataPackEntry* found_entry = nullptr;
    for (FxDataPackEntry& entry : Entries) {
        FxLogDebug("Check Entry: {:020}, {:020}", entry.Id, id);
        if (entry.Id == id) {
            found_entry = &entry;
            break;
        }
    }

    return found_entry;
}

void FxDataPack::WriteToFile(const char* name)
{
    File.Open(name, FxFile::eWrite, FxFile::eText);

    if (!File.IsFileOpen()) {
        return;
    }


    TextWriteHeader();
    TextWriteData();

    File.Close();
}


void FxDataPack::ReadFromFile(const char* name)
{
    File.Open(name, FxFile::eRead, FxFile::eText);
    if (!File.IsFileOpen()) {
        return;
    }

    TextReadHeader();
    // TextReadAllData(file);
}


FxDataPack::~FxDataPack()
{
    if (File.IsFileOpen()) {
        File.Close();
    }
}
