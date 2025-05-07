#pragma once

#include "FxBaseLoader.hpp"

#include <string>

struct cgltf_data;

class FxGltfLoader : public FxBaseLoader
{
public:
    using Status = FxBaseLoader::Status;

    FxGltfLoader() = default;

    Status LoadFromFile(FxBaseAsset *asset, std::string path) override;
    void Destroy(FxBaseAsset *asset) override;

    ~FxGltfLoader() = default;

protected:
    void CreateGpuResource(FxBaseAsset *asset) override;

private:
    cgltf_data *mGltfData = nullptr;
};
