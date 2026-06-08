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

    Path& Add(const String& sub);

    Path operator/(const String& sub) const;
    Path& operator=(Path&& other);

    const String& BaseName() const;

    String Str() const;


public:
    ComponentList Components;
};


} // namespace fx
