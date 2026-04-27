#pragma once

#include "ConfigFile.hpp"

#include <Scene.hpp>

namespace fx {

class SceneFile
{
public:
    SceneFile() = default;

    // void Save(const Scene& scene);

    void Load(const std::string& path, Scene& scene);

private:
    void AddObjectFromEntry(const std::string& path, const ConfigEntry& object, Scene& scene);
    void AddColliderFromEntry(const std::string& path, const ConfigEntry& collider, Scene& scene);

    void ApplyPropertiesToObject(TSRef<Object>& object, const ConfigEntry& object_entry);

public:
    // ConfigFile InfoFile;
};

} // namespace fx
