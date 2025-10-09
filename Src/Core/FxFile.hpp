#pragma once

#include <stdio.h>

#include <Core/FxSlice.hpp>
#include <Core/FxTypes.hpp>

class FxFile
{
public:
public:
    FxFile() {}
    FxFile(const char* path, const char* mode);

    void Open(const char* path, const char* mode);
    uint64 GetFileSize();

    FxSlice<char> ReadRaw();

    template <typename TDataType>
    FxSlice<TDataType> Read()
    {
        return FxSlice(ReadRaw());
    }

    void WriteRaw(const void* data, uint64 size);

    template <typename TDataType>
    void Write(TDataType* data, uint64 size)
    {
        WriteRaw(reinterpret_cast<const void*>(data), size);
    }

    void Close();

    ~FxFile() { Close(); }

public:
    FILE* pFileHandle = nullptr;

private:
    uint64 mFileSize = 0;
};
