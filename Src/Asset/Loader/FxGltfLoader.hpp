#pragma once

#include "FxBaseLoader.hpp"
#include <FxMaterial.hpp>

#include <string>

struct cgltf_data;
struct cgltf_texture_view;

struct FxGltfResource
{
    uint8* DecodedData = nullptr;

};

class FxGltfLoader : public FxBaseLoader
{
public:
    using Status = FxBaseLoader::Status;

    FxGltfLoader() = default;

    Status LoadFromFile(FxRef<FxBaseAsset> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<FxBaseAsset> asset, const uint8* data, uint32 size) override;

    void Destroy(FxRef<FxBaseAsset>& asset) override;

    ~FxGltfLoader() = default;

protected:
    void CreateGpuResource(FxRef<FxBaseAsset>& asset) override;

    FxRef<FxImage> LoadTexture(const FxRef<FxMaterial>& material, const cgltf_texture_view& texture_view);

private:
    cgltf_data *mGltfData = nullptr;
};
