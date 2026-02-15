#pragma once

#include "FxConfigFile.hpp"

#include <FxScene.hpp>

class FxSceneFile
{
public:
    FxSceneFile() = default;

    // void Save(const FxScene& scene);

    void Load(const std::string& path, FxScene& scene);

private:
    void AddObjectFromEntry(const std::string& path, const FxConfigEntry& object, FxScene& scene);

public:
    // FxConfigFile InfoFile;
};
