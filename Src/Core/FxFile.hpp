#pragma once

#include <stdio.h>

#include <Core/FxSlice.hpp>
#include <Core/FxTypes.hpp>
#include <Math/FxMathUtil.hpp>
#include <filesystem>

class FxFile
{
public:
public:
    FxFile() {}
    FxFile(const char* path, const char* mode);


    static std::filesystem::file_time_type GetLastModifiedTime(const std::string& path);

    void Open(const char* path, const char* mode);
    uint64 GetFileSize();

    template <typename TDataType>
    FxSlice<TDataType> Read(const FxSlice<TDataType>& out_buffer)
    {
        if (!pFileHandle) {
            return nullptr;
        }

        constexpr uint32 type_size = sizeof(TDataType);

        const uint64 n_to_read = std::fmin(GetFileSize() / type_size, out_buffer.Size);
        uint64 items_read = fread(out_buffer.Ptr, type_size, n_to_read, pFileHandle);

        return FxMakeSlice<TDataType>(out_buffer.Ptr, items_read);
    }

    template <typename TDataType>
    FxSlice<TDataType> Read()
    {
        if (!pFileHandle) {
            return nullptr;
        }

        const uint64 buffer_size = FxMath::AlignValue<sizeof(TDataType)>(GetFileSize());
        TDataType* buffer = FxMemPool::Alloc<TDataType>(buffer_size);

        return Read(FxMakeSlice(buffer, buffer_size));
    }

    void WriteRaw(const void* data, uint64 size);

    template <typename TDataType>
    void Write(TDataType* data, uint64 size)
    {
        WriteRaw(reinterpret_cast<const void*>(data), size);
    }

    bool IsFileOpen() const { return (pFileHandle != nullptr); }

    void Write(char ch);
    void Flush();

    void SeekTo(uint64 index);
    void SeekBy(uint32 by);
    void SeekToEnd();


    void Close();

    ~FxFile() { Close(); }

public:
    FILE* pFileHandle = nullptr;

private:
    uint64 mFileSize = 0;
    bool mbSizeOutOfDate : 1 = true;
};
