#pragma once

#include <Core/PagedArray.hpp>
//
#include "Types.hpp"

#include <Core/String.hpp>

namespace fx {

/////////////////////////////////////
// Path functions
/////////////////////////////////////

class FilePath
{
public:
    FilePath() = default;
    FilePath(String value) : Value(value) {};

    FilePath operator/(const String& other) const;

    FilePath RemoveExtension() const;

    /**
     * @brief Removes the filename and the file extension from the path.
     *
     * Example: Some/Path/Filename.ext  becomes  Some/Path
     */
    FilePath RemoveFilename() const;

    FilePath GetFilename(bool keep_extension) const;

    FX_FORCE_INLINE String& Str() { return Value; }
    FX_FORCE_INLINE const String& Str() const { return Value; }

    FX_FORCE_INLINE const char* CStr() const { return Value.CStr(); }

public:
    String Value;
};


namespace FilesystemIO {

/////////////////////////////////////
// Base path functions
/////////////////////////////////////

/**
 * @brief Returns the directory containing the running executable (no trailing separator).
 */
const char* GetExecutablePath();

/**
 * @brief Returns the engine's base directory (FX_BASE_DIR) that assets are authored relative to.
 */
const char* GetBasePath();

/**
 * @brief Resolves `relative_path` against `GetExecutablePath()` first, falling back to
 * `GetBasePath()` if no file exists there. Lets a packaged build ship assets next to the
 * executable while dev builds keep loading them from the repo root.
 */
std::string ResolvePath(const std::string& relative_path);

/////////////////////////////////////
// File functions
/////////////////////////////////////

uint64 FileGetLastModified(const char* path);
bool FileExists(const String& path);

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
