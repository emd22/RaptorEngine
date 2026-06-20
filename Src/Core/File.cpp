#include "File.hpp"

#include <Core/Defines.hpp>
#include <Core/MemPool/MemPool.hpp>

namespace fx {

using eDataType = File::eDataType;
using eModType = File::eModType;

File::File(const String& path, File::eModType mt, File::eDataType dt) { Open(path, mt, dt); }

static const char* MakeModeStr(File::eModType mt, File::eDataType dt)
{
    if (dt == eDataType::Binary) {
        if (mt == eModType::Read) {
            return "rb";
        }
        else if (mt == eModType::Write) {
            return "wb";
        }
    }

    if (mt == eModType::Read) {
        return "r";
    }
    else if (mt == eModType::Write) {
        return "w";
    }

    return "r";
}

void File::Open(const String& path, eModType mt, eDataType dt)
{
    mModType = mt;
    mDataType = dt;

    const char* mode = MakeModeStr(mt, dt);

    // If the file handle is already set, reopen it with the new path or mode.
    if (pFileHandle != nullptr) {
#ifdef FX_PLATFORM_WINDOWS
        errno_t result = freopen_s(&pFileHandle, path.CStr(), mode, pFileHandle);
        if (result) {
            pFileHandle = nullptr;
        }

        return;
#else
        freopen(path.CStr(), mode, pFileHandle);
        return;
#endif
    }

#ifdef FX_PLATFORM_WINDOWS
    errno_t result = fopen_s(&pFileHandle, path.CStr(), mode);

    if (result) {
        pFileHandle = nullptr;
    }

#else
    pFileHandle = fopen(path.CStr(), mode);
    if (pFileHandle == nullptr) {
        LogError(LC_CORE, "Error opening file {}", path);
    }
#endif
}

uint64 File::GetFileSize()
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

void File::Write(const char* str)
{
    if (!pFileHandle) {
        LogWarning(LC_CORE, spcErrCannotWriteUnopened);
        return;
    }

    fputs(str, pFileHandle);
}

void File::WriteRaw(const void* data, uint64 size)
{
    if (!pFileHandle) {
        LogWarning(LC_CORE, spcErrCannotWriteUnopened);
        return;
    }

    fwrite(data, 1, size, pFileHandle);
}

void File::SeekTo(uint64 index) { fseek(pFileHandle, index, SEEK_SET); }
void File::SeekToEnd() { fseek(pFileHandle, 0, SEEK_END); }
void File::SeekBy(uint32 by) { fseek(pFileHandle, by, SEEK_CUR); }

uint64 File::GetPosition() const { return ftell(pFileHandle); }


void File::Flush()
{
    if (!pFileHandle) {
        return;
    }
    fflush(pFileHandle);
}

void File::Write(char ch)
{
    if (!pFileHandle) {
        return;
    }
    fputc(ch, pFileHandle);
}

void File::Close()
{
    if (pFileHandle == nullptr) {
        return;
    }

    fclose(pFileHandle);
    pFileHandle = nullptr;
}

#include <filesystem>


std::filesystem::file_time_type File::GetLastModifiedTime(const std::string& path)
{
    return std::filesystem::last_write_time(path);
}

} // namespace fx
