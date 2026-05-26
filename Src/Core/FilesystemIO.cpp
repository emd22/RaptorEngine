#include "FilesystemIO.hpp"

#include <filesystem>

namespace fx::FilesystemIO {

/////////////////////////////////////
// File functions
/////////////////////////////////////

uint64 FileGetLastModified(const char* path)
{
    return std::filesystem::last_write_time(path).time_since_epoch().count();
}

bool FileExists(const char* path) { return std::filesystem::exists(path); }

String RemoveExtension(const String& path)
{
    const char* ptr = path.CStr();
    const uint32 length = path.GetLength();

    int32 ext_index = 0;

    // Find the first occurance of a period
    for (; ext_index < length; ext_index++) {
        if (ptr[ext_index] == '.') {
            break;
        }
    }

    return path.SubStr(0, ext_index);
}

String FilenameFromPath(const String& path, bool keep_extension)
{
    const char* ptr = path.CStr();
    const uint32 length = path.GetLength();

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

    return path.SubStr(slash_index, extension_index);
}

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

}; // namespace fx::FilesystemIO
