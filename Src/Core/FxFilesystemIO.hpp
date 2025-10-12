#pragma once

#include "FxPagedArray.hpp"
#include "FxTypes.hpp"

class FxFilesystemIO
{
public:
    static uint64 FileGetLastModified(const char* path);
    static FxPagedArray<std::string> DirList(const char* path, const char* ends_with = nullptr);
    static FxPagedArray<std::string> DirListIfHasExtension(const char* path, const std::string& required_extension,
                                                           bool return_filename_only);
};
