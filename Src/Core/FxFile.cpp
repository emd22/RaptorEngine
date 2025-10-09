#include "FxFile.hpp"

#include <Core/FxDefines.hpp>
#include <Core/MemPool/FxMemPool.hpp>

FxFile::FxFile(const char* path, const char* mode) { Open(path, mode); }

void FxFile::Open(const char* path, const char* mode)
{
    if (pFileHandle != nullptr) {
        Close();
    }

#ifdef FX_PLATFORM_WINDOWS
    errno_t result = fopen_s(&pFileHandle, path, mode);

    if (!result) {
        pFileHandle = nullptr;
    }

#else
    pFileHandle = fopen(path, mode);
#endif
}

uint64 FxFile::GetFileSize()
{
    if (pFileHandle == nullptr) {
        return 0;
    }

    if (mFileSize != 0) {
        return mFileSize;
    }

    // TODO: Use OS file size functions for an estimated size (inaccurate on some platforms)

    fseek(pFileHandle, 0, SEEK_END);
    mFileSize = ftell(pFileHandle);
    rewind(pFileHandle);

    return mFileSize;
}

FxSlice<char> FxFile::ReadRaw()
{
    if (!pFileHandle) {
        return nullptr;
    }

    char* buffer = FxMemPool::Alloc<char>(GetFileSize());
    uint64 bytes_read = fread(buffer, 1, GetFileSize(), pFileHandle);

    return FxMakeSlice<char>(buffer, bytes_read);
}

void FxFile::WriteRaw(const void* data, uint64 size)
{
    if (!pFileHandle) {
        return;
    }

    fwrite(data, 1, size, pFileHandle);
}

void FxFile::Close()
{
    if (pFileHandle == nullptr) {
        return;
    }

    fclose(pFileHandle);
    pFileHandle = nullptr;
}
