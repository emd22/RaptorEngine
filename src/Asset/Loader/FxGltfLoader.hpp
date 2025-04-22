#pragma once

#include "FxBaseLoader.hpp"

#include <string>

class FxGltfLoader : public FxBaseLoader
{
public:
    using Status = FxBaseLoader::Status;

    FxGltfLoader() = default;

    Status LoadFromFile(FxBaseAsset *asset, std::string path) override;
    void Destroy(FxBaseAsset *asset) override;

    ~FxGltfLoader() = default;
};
