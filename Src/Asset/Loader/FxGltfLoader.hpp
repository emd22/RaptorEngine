#pragma once

#include "FxBaseLoader.hpp"

#include <string>

struct cgltf_data;

class FxGltfLoader : public FxBaseLoader
{
public:
    using Status = FxBaseLoader::Status;

    FxGltfLoader() = default;

    Status LoadFromFile(FxRef<FxBaseAsset>& asset, const std::string& path) override;
    void Destroy(FxRef<FxBaseAsset>& asset) override;

    ~FxGltfLoader() = default;

protected:
    void CreateGpuResource(FxRef<FxBaseAsset>& asset) override;

private:
    cgltf_data *mGltfData = nullptr;
};
