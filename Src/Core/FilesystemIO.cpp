#include "FilesystemIO.hpp"

#include <Core/Defines.hpp>
#include <cstring>
#include <filesystem>

#ifdef FX_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(FX_PLATFORM_MACOS)
#include <mach-o/dyld.h>
#elif defined(FX_PLATFORM_LINUX)
#include <unistd.h>
#endif

namespace fx {

FilePath FilePath::operator/(const String& other) const { return FilePath(String::Fmt("{}/{}", Value, other)); }

FilePath FilePath::RemoveExtension() const { return FilePath(Value.SubStrAbs(0, Value.FindFirst('.'))); }
FilePath FilePath::RemoveFilename() const { return FilePath(Value.SubStrAbs(0, Value.FindLast('/'))); }

FilePath FilePath::GetFilename(bool keep_extension) const
{
    const char* ptr = Value.CStr();
    const uint32 length = Value.GetLength();

    int32 extension_index = 0;
    int32 slash_index = 0;

    // Move backwards through the string to find the leftmost period and the rightmost slash.
    // On the first occurance of a slash, break as that is our start point.`
    for (int32 index = length - 1; index > 0; index--) {
        char ch = ptr[index];

        if (ch == '.') {
            extension_index = index;
        }
        else if (ch == '/') {
            // + 1 to omit the slash
            slash_index = index + 1;
            break;
        }
    }

    // If there is no extension found (index is zero) then take the entire basename.
    // Note that if there is no slash found, then the entire string is returned as slash_index defaults to zero.
    if (keep_extension || extension_index == 0) {
        extension_index = length;
    }

    return FilePath(Value.SubStrAbs(slash_index, extension_index));
}


namespace FilesystemIO {

/////////////////////////////////////
// Base path functions
/////////////////////////////////////

static std::string QueryExecutableDir()
{
    constexpr size_t cMaxPathLength = 4096;
    char buffer[cMaxPathLength];

#ifdef FX_PLATFORM_WINDOWS
    DWORD length = GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(cMaxPathLength));
    if (length == 0 || length == cMaxPathLength) {
        return std::string();
    }
#elif defined(FX_PLATFORM_MACOS)
    uint32_t size = static_cast<uint32_t>(cMaxPathLength);
    if (_NSGetExecutablePath(buffer, &size) != 0) {
        return std::string();
    }
    size_t length = strlen(buffer);
#elif defined(FX_PLATFORM_LINUX)
    ssize_t result = readlink("/proc/self/exe", buffer, cMaxPathLength - 1);
    if (result <= 0) {
        return std::string();
    }
    size_t length = static_cast<size_t>(result);
#endif

    return std::filesystem::path(std::string(buffer, length)).parent_path().string();
}

const char* GetExecutablePath()
{
    static const std::string sExecutableDir = QueryExecutableDir();
    return sExecutableDir.c_str();
}

const char* GetBasePath() { return FX_BASE_DIR; }

std::string ResolvePath(const std::string& relative_path)
{
    const std::string exe_relative = std::string(GetExecutablePath()) + "/" + relative_path;

    if (FileExists(exe_relative)) {
        return exe_relative;
    }

    return std::string(GetBasePath()) + "/" + relative_path;
}

/////////////////////////////////////
// File functions
/////////////////////////////////////

uint64 FileGetLastModified(const char* path)
{
    return std::filesystem::last_write_time(path).time_since_epoch().count();
}

bool FileExists(const String& path) { return std::filesystem::exists(path.CStr()); }

/////////////////////////////////////
// Directory functions
/////////////////////////////////////

PagedArray<std::string> DirList(const char* path, const char* ends_with)
{
    const std::filesystem::directory_iterator& dir_iterator = std::filesystem::directory_iterator(path);

    PagedArray<std::string> file_paths(16);

    for (const auto& entry : dir_iterator) {
        const std::string& entry_str = entry.path().filename().string();

        if (ends_with) {
            if (entry_str.ends_with(ends_with)) {
                file_paths.Insert(entry_str);
            }

            continue;
        }

        file_paths.Insert(entry_str);
    }

    return file_paths;
}

std::string DirCurrent() { return std::filesystem::current_path().string(); }

PagedArray<std::string> DirListIfHasExtension(const char* path, const std::string& required_extension,
                                              bool return_filename_only)
{
    const std::filesystem::directory_iterator& dir_iterator = std::filesystem::directory_iterator(path);

    PagedArray<std::string> file_paths(16);

    for (const std::filesystem::directory_entry& entry : dir_iterator) {
        const auto& path_obj = entry.path();

        if (path_obj.has_extension() && path_obj.extension().string() == required_extension) {
            if (return_filename_only) {
                file_paths.Insert(path_obj.stem().string());
            }
            else {
                file_paths.Insert(path_obj.filename().string());
            }
        }
    }

    return file_paths;
}

void DirCreate(const char* path) { std::filesystem::create_directory(path); }

} // namespace FilesystemIO

} // namespace fx
