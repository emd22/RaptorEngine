#pragma once

#include "FxLoaderBase.hpp"

#include <FxMaterial.hpp>
#include <FxObject.hpp>
#include <string>

struct cgltf_data;
struct cgltf_material;
struct cgltf_mesh;
struct cgltf_texture_view;
struct cgltf_primitive;

struct FxGltfMaterialToLoad
{
    //    cgltf_material* GltfMaterial = nullptr;
    FxRef<FxObject> Object { nullptr };
    int PrimitiveIndex = 0;
    int MeshIndex = 0;
};

class FxLoaderGltf : public FxLoaderBase
{
public:
    using Status = FxLoaderBase::Status;

    FxLoaderGltf() = default;

    Status LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size) override;

    void UploadMeshToGpu(FxRef<FxObject>& object, cgltf_mesh* gltf_mesh, int mesh_index);

    //    void LoadAttachedMaterials();

    void Destroy(FxRef<FxAssetBase>& asset) override;

    ~FxLoaderGltf() override = default;

private:
    void MakeEmptyMaterialTexture(FxRef<FxMaterial>& material, FxMaterialComponent& component);
    void MakeMaterialForPrimitive(FxRef<FxObject>& object, cgltf_primitive* primitive);


public:
    std::vector<FxGltfMaterialToLoad> MaterialsToLoad;

    bool KeepInMemory = false;

    FxSizedArray<RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>> VertexBuffer;
    FxSizedArray<uint32> IndexBuffer;

protected:
    void CreateGpuResource(FxRef<FxAssetBase>& asset) override;

    // void UnpackMeshAttributes(FxRef<FxPrimitiveMesh<>>& mesh, cgltf_primitive* primitive);

    // static FxRef<FxAssetImage> LoadTexture(const FxRef<FxMaterial>& material, const cgltf_texture_view&
    // texture_view);

private:
    cgltf_data* mGltfData = nullptr;
};
