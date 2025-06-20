#pragma once

#include "FxBaseLoader.hpp"

#include <string>

struct cgltf_data;

class FxGltfLoader : public FxBaseLoader
{
public:
    using Status = FxBaseLoader::Status;

    FxGltfLoader() = default;

    Status LoadFromFile(std::shared_ptr<FxBaseAsset>& asset, const std::string& path) override;
    void Destroy(std::shared_ptr<FxBaseAsset>& asset) override;

    ~FxGltfLoader() = default;

protected:
    void CreateGpuResource(std::shared_ptr<FxBaseAsset>& asset) override;

private:
    cgltf_data *mGltfData = nullptr;
};
