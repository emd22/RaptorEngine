#pragma once

#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <vector>

namespace fx {

class Path
{
    using ComponentList = std::vector<String>;

public:
    enum class eStrMode
    {
        FullPath,
        NoBaseName,
    };

public:
    Path() = default;
    /**
     * @brief Separates components to create a Path object given the file path `path`.
     */
    Path(const String& full_path);
    Path(const ComponentList& components);
    Path(Path&& other);

    /**
     * @brief Adds a new component to the end of the path.
     */
    Path& Add(const String& sub);

    /**
     * @brief Removes the component at the given index
     */
    Path& Remove(const uint32 index);

    /**
     * @brief Removes the parent directory from the path, preserving the filename.
     * Example:
     * `A/B/File.txt` becomes `A/File.txt`.
     */
    Path& DirUp();

    /**
     * @brief Adds a directory component at the end of the path, preserving the filename.
     * Example:
     * `A/File.txt` becomes `A/B/File.txt`.`
     */
    Path& DirDown(const String& dir_name);

    Path& RemoveLast()
    {
        Remove(Components.size() - 1);
        return *this;
    }

    Path& RemoveFirst()
    {
        Remove(0);
        return *this;
    }

    Path& RemoveExtension();
    Path& AddExtension(const String& ext);
    Path& SetExtension(const String& ext);
    bool HasExtension() const;

    void CreateDirs() const;

    Path operator/(const String& sub) const;
    Path& operator=(Path&& other);

    String* Get(const uint32 index);
    const String* Get(const uint32 index) const;
    String* BaseName() { return Get(Components.size() - 1); };

    String Str(eStrMode mode = eStrMode::FullPath) const;


public:
    ComponentList Components;

    bool bIsAbsolutePath = false;
};


} // namespace fx
