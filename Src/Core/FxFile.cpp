#include "FxFile.hpp"

#include <Core/FxDefines.hpp>
#include <Core/MemPool/FxMemPool.hpp>

FxFile::FxFile(const char* path, const char* mode) { Open(path, mode); }

void FxFile::Open(const char* path, const char* mode)
{
    // If the file handle is already set, reopen it with the new path or mode.
    if (pFileHandle != nullptr) {
#ifdef FX_PLATFORM_WINDOWS
        errno_t result = freopen_s(&pFileHandle, path, mode, pFileHandle);
        if (!result) {
            pFileHandle = nullptr;
        }

        return;
#else
        freopen(path, mode, pFileHandle);
        return;
#endif
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

    if (mbSizeOutOfDate) {
        uint64 current_seek = ftell(pFileHandle);

        SeekToEnd();
        mFileSize = ftell(pFileHandle);
        SeekTo(current_seek);

        mbSizeOutOfDate = false;
    }

    return mFileSize;
}

void FxFile::WriteRaw(const void* data, uint64 size)
{
    if (!pFileHandle) {
        FxLogWarning("Cannot write to file as it has not been opened!");
        return;
    }

    fwrite(data, 1, size, pFileHandle);
}

void FxFile::SeekTo(uint64 index) { fseek(pFileHandle, index, SEEK_SET); }
void FxFile::SeekToEnd() { fseek(pFileHandle, 0, SEEK_END); }
void FxFile::SeekBy(uint32 by) { fseek(pFileHandle, by, SEEK_CUR); }

void FxFile::Flush()
{
    if (!pFileHandle) {
        return;
    }
    fflush(pFileHandle);
}

void FxFile::Write(char ch)
{
    if (!pFileHandle) {
        return;
    }
    fputc(ch, pFileHandle);
}

void FxFile::Close()
{
    if (pFileHandle == nullptr) {
        return;
    }

    fclose(pFileHandle);
    pFileHandle = nullptr;
}

#include <filesystem>


std::filesystem::file_time_type FxFile::GetLastModifiedTime(const std::string& path)
{
    return std::filesystem::last_write_time(path);
}
