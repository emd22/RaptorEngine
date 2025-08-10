#pragma once

#include "FxLoaderBase.hpp"
#include <FxMaterial.hpp>
#include <FxObject.hpp>

#include <string>

struct cgltf_data;
struct cgltf_material;
struct cgltf_mesh;
struct cgltf_texture_view;

struct FxGltfMaterialToLoad
{
//    cgltf_material* GltfMaterial = nullptr;
    FxRef<FxObject> Object{nullptr};
    int PrimitiveIndex = 0;
    int MeshIndex = 0;
};

/*
 4194304
 2793472
 194560
 7454720
 */

class FxLoaderGltf : public FxLoaderBase
{
public:
    using Status = FxLoaderBase::Status;

    FxLoaderGltf() = default;

    Status LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size) override;
    
    void UploadMeshToGpu(FxRef<FxObject>& object, cgltf_mesh *gltf_mesh, int mesh_index);
    
    

//    void LoadAttachedMaterials();
    
    void Destroy(FxRef<FxAssetBase>& asset) override;

    ~FxLoaderGltf() override = default;
    
public:
    std::vector<FxGltfMaterialToLoad> MaterialsToLoad;

protected:
    void CreateGpuResource(FxRef<FxAssetBase>& asset) override;

    // static FxRef<FxAssetImage> LoadTexture(const FxRef<FxMaterial>& material, const cgltf_texture_view& texture_view);

private:
    cgltf_data* mGltfData = nullptr;
    
};
