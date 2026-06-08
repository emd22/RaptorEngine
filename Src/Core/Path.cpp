#include "Path.hpp"


namespace fx {

const String Path::scNullComponent = String(nullptr, 0);

Path::Path(const String& full_path)
{
    uint32 start = 0;
    uint32 end = 0;

    Components.reserve(10);

    // Break into path components

    const char* pstr = full_path.CStr();

    while ((end = full_path.FindNext(start, '/')) != String::scNotFound) {
        // If this component starts with a slash, skip it.
        // This applies to paths like `/SomeFile.txt` and `Paths//SomeFile.txt.`
        if (pstr[start] == '/') {
            // Skip the slash character, and find the new end index.
            start = start + 1;
            continue;
        }

        const uint32 length = end - start;
        Components.emplace_back(pstr + start, length);

        start = end + 1;
    }

    // Push the final component (the basename)
    if (start < full_path.GetLength()) {
        const uint32 length = full_path.GetLength() - start;
        Components.emplace_back(pstr + start, length);
    }

    Components.shrink_to_fit();

    for (const String& pc : Components) {
        LogInfo("Component: {}", pc);
    }
}

Path::Path(const ComponentList& components) { Components = components; }

Path::Path(Path&& other) { (*this) = std::move(other); }

Path& Path::Add(const String& sub)
{
    Components.push_back(sub);
    return *this;
}

Path& Path::operator=(Path&& other)
{
    Components = std::move(other.Components);
    return *this;
}

Path Path::operator/(const String& sub) const
{
    Path np(Components);
    np.Add(sub);
    return std::move(np);
}


const String& Path::BaseName() const
{
    if (Components.size() < 1) {
        return Path::scNullComponent;
    }

    return *Components.end();
}

String Path::Str() const
{
    String result;

    const uint32 num_components = Components.size();

    for (uint32 i = 0; i < num_components; i++) {
        result += Components[i];

        if (i < num_components - 1) {
            result += String("/", 1);
        }
    }

    return result;
}

} // namespace fx
