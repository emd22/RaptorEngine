#pragma once

#include "FxConfigFile.hpp"

#include <FxObject.hpp>

class FxLevelFile
{
public:
    FxLevelFile() = default;
    // FxLevel(const std::string& name);
    //
    void Save(const FxObject& obj);

public:
    FxConfigFile InfoFile;
};
