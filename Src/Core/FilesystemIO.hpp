#pragma once

#include <Core/PagedArray.hpp>
//
#include "Types.hpp"

#include <Core/String.hpp>

namespace fx {

namespace FilesystemIO {

/////////////////////////////////////
// File functions
/////////////////////////////////////

uint64 FileGetLastModified(const char* path);
bool FileExists(const char* path);

/////////////////////////////////////
// Path functions
/////////////////////////////////////

String RemoveExtension(const String& path);
String FilenameFromPath(const String& path, bool keep_extension);

//////////////////////////////
// Directory functions
//////////////////////////////

PagedArray<std::string> DirList(const char* path, const char* ends_with = nullptr);
std::string DirCurrent();
PagedArray<std::string> DirListIfHasExtension(const char* path, const std::string& required_extension,
                                              bool return_filename_only);

void DirCreate(const char* path);


}; // namespace FilesystemIO

} // namespace fx
