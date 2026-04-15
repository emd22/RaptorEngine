#pragma once

#include "PagedArray.hpp"
//
#include "File.hpp"
#include "Hash.hpp"

namespace fx {

struct BasicDbEntry
{
    Hash64 KeyHash;
    std::string Value;
};


class BasicDb
{
private:
public:
    void Open(const char* path);

    void WriteEntry(const BasicDbEntry& entry);
    BasicDbEntry* FindEntry(Hash64 key);

    void SaveToFile();

    bool IsOpen() const { return mbIsOpen; }

    void Close();
    ~BasicDb() { Close(); }

private:
    void Parse();


private:
    File mInFile;
    File mOutFile;

    char* mpData = nullptr;

    std::string mPath;
    PagedArray<BasicDbEntry> mEntryMarkers;

    bool mbPendingSaveToFile : 1 = false;
    bool mbIsOpen : 1 = false;
};

} // namespace fx
