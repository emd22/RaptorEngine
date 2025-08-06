#pragma once

#include "FxLoaderBase.hpp"
#include <FxMaterial.hpp>

#include <string>

struct cgltf_data;
struct cgltf_texture_view;

struct FxGltfResource
{
    uint8* DecodedData = nullptr;

};

class FxLoaderGltf : public FxLoaderBase
{
public:
    using Status = FxLoaderBase::Status;

    FxLoaderGltf() = default;

    Status LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size) override;

    void Destroy(FxRef<FxAssetBase>& asset) override;

    ~FxLoaderGltf() = default;

protected:
    void CreateGpuResource(FxRef<FxAssetBase>& asset) override;

    FxRef<FxAssetImage> LoadTexture(const FxRef<FxMaterial>& material, const cgltf_texture_view& texture_view);

private:
    cgltf_data *mGltfData = nullptr;
};
