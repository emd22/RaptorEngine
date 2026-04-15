#pragma once

#include <Core/PagedArray.hpp>
//
#include "Types.hpp"

namespace fx {

namespace FilesystemIO {


uint64 FileGetLastModified(const char* path);

//////////////////////////////
// Directory functions
//////////////////////////////

PagedArray<std::string> DirList(const char* path, const char* ends_with = nullptr);
std::string DirCurrent();
PagedArray<std::string> DirListIfHasExtension(const char* path, const std::string& required_extension,
                                              bool return_filename_only);


}; // namespace FilesystemIO

} // namespace fx
