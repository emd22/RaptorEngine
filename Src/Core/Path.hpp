#pragma once

#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <vector>

namespace fx {

class Path
{
    static const String scNullComponent;

    using ComponentList = std::vector<String>;

public:
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

    Path operator/(const String& sub) const;
    Path& operator=(Path&& other);

    const String& BaseName() const { return Get(Components.size() - 1); };
    const String& Get(const uint32 index) const;
    String* Get(const uint32 index);

    String Str() const;


public:
    ComponentList Components;
};


} // namespace fx
