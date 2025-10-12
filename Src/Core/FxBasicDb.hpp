#pragma once

#include "FxFile.hpp"
#include "FxHash.hpp"
#include "FxPagedArray.hpp"

struct FxBasicDbEntry
{
    FxHash KeyHash;
    std::string Value;
};


class FxBasicDb
{
private:
public:
    void Open(const char* path);

    void WriteEntry(const FxBasicDbEntry& entry);
    FxBasicDbEntry* FindEntry(FxHash key);

    void SaveToFile();

    bool IsOpen() const { return mbIsOpen; }

    void Close();
    ~FxBasicDb() { Close(); }

private:
    void Parse();


private:
    FxFile mInFile;
    FxFile mOutFile;

    char* mpData = nullptr;

    std::string mPath;
    FxPagedArray<FxBasicDbEntry> mEntryMarkers;

    bool mbPendingSaveToFile : 1 = false;
    bool mbIsOpen : 1 = false;
};
