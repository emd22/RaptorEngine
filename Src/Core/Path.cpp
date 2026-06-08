#include "Path.hpp"


namespace fx {

const String Path::scNullComponent = String(nullptr, 0);

Path::Path(const String& full_path)
{
    uint32 start = 0;
    uint32 end = 0;

    // Windows paths suck ass
    // Check if there is a windows style path in the provided path string.
    bool check_win_style = full_path.FindNext(0, "\\") != String::scNotFound;

    // Reserve some memory for components so we dont need to resize the buffer constantly
    // We will shrink this once we are finished.
    Components.reserve(10);

    // Break into path components

    const char* pstr = full_path.CStr();

    while (true) {
        end = full_path.FindNext(start, '/');

        if (check_win_style) {
            const uint32 win_end = full_path.FindNext(start, "\\");

            // There is a closer win path style, choose that.
            if (win_end < end) {
                end = win_end;
            }
        }

        // There was no suitable next path, break
        if (end == String::scNotFound) {
            break;
        }

        // If this component starts with a slash, skip it.
        // This applies to paths like `/SomeFile.txt` and `Paths//SomeFile.txt.`
        if (pstr[start] == '/' || pstr[start] == '\\') {
            // Skip the slash character, and find the new end index.
            start = start + 1;
            continue;
        }

        // Finally, push the path component to the components list
        const uint32 length = end - start;
        Components.emplace_back(pstr + start, length);

        // +1 to avoid the slash. Consecutive slashes are dealt with in the next loop iteration
        start = end + 1;
    }

    // Push the final component (the basename)
    if (start < full_path.GetLength()) {
        const uint32 length = full_path.GetLength() - start;
        Components.emplace_back(pstr + start, length);
    }

    Components.shrink_to_fit();
}

Path::Path(const ComponentList& components) { Components = components; }

Path::Path(Path&& other) { (*this) = std::move(other); }

Path& Path::Add(const String& sub)
{
    Components.push_back(sub);
    return *this;
}

Path& Path::Remove(const uint32 index)
{
    if (index >= Components.size()) {
        return *this;
    }

    Components.erase(Components.begin() + index);

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


const String& Path::Get(const uint32 index) const
{
    if (index >= Components.size()) {
        return Path::scNullComponent;
    }

    return Components[index];
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
