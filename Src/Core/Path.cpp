#include "Path.hpp"

#include <filesystem>

namespace fx {


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

    bIsAbsolutePath = (full_path.Length > 0 && pstr[0] == '/');

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

Path& Path::DirBack()
{
    uint32 last_index = Components.size() - 1;

    // If there are no items left, do nothing
    if (last_index == 0) {
        return *this;
    }

    // Remove the directory before the basename
    Components.erase(Components.begin() + (last_index - 1));

    return *this;
}


void Path::CreateDirs() const
{
    String path_str = Str();
    if (!std::filesystem::exists(path_str.CStr())) {
        std::filesystem::create_directories(path_str.CStr());
    }
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


String* Path::Get(const uint32 index)
{
    if (index >= Components.size()) {
        return nullptr;
    }

    return &Components[index];
}

Path& Path::RemoveExtension()
{
    String* basename = Get(Components.size() - 1);

    if (!basename) {
        return *this;
    }

    const uint32 ext_index = basename->FindFirst('.');

    // No extension here boss
    if (ext_index == String::scNotFound) {
        return *this;
    }

    basename->ShortenTo(ext_index);

    return *this;
}


String Path::Str() const
{
    String result;

    const uint32 num_components = Components.size();

    if (bIsAbsolutePath) {
        result = "/";
    }

    for (uint32 i = 0; i < num_components; i++) {
        result += Components[i];

        if (i < num_components - 1) {
            result += String("/", 1);
        }
    }

    return result;
}

} // namespace fx
