#pragma once

#include "AxLoaderBase.hpp"

#include <FxMaterial.hpp>
#include <FxObject.hpp>
#include <string>

struct cgltf_data;
struct cgltf_material;
struct cgltf_mesh;
struct cgltf_texture_view;
struct cgltf_primitive;
struct cgltf_skin;

struct AxGltfMaterialToLoad
{
    FxRef<FxObject> pObject { nullptr };
    int PrimitiveIndex = 0;
    int MeshIndex = 0;
};

class AxLoaderGltf : public AxLoaderBase
{
public:
    using Status = AxLoaderBase::Status;

    AxLoaderGltf() = default;

    Status LoadFromFile(FxRef<AxBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<AxBase> asset, const uint8* data, uint32 size) override;

    void UploadMeshToGpu(FxRef<FxObject>& object, cgltf_mesh* gltf_mesh, int mesh_index);

    void Destroy(FxRef<AxBase>& asset) override;

    ~AxLoaderGltf() override = default;

private:
    // void MakeEmptyMaterialTexture(FxRef<FxMaterial>& material, FxMaterialComponent& component);
    void MakeMaterialForPrimitive(FxRef<FxObject>& object, cgltf_primitive* primitive);

    void UnpackMeshAttributes(const FxRef<FxObject>& object, FxRef<FxPrimitiveMesh<>>& mesh,
                              cgltf_primitive* primitive);

    void LoadAnimations();
    void LoadAnimationSkin(FxRef<FxPrimitiveMesh<>>& mesh, cgltf_skin* skin);

public:
    std::vector<AxGltfMaterialToLoad> MaterialsToLoad;

    bool bKeepInMemory = false;

    FxSizedArray<RxVertexDefault> VertexBuffer;
    FxSizedArray<uint32> IndexBuffer;

protected:
    void CreateGpuResource(FxRef<AxBase>& asset) override;

    // void UnpackMeshAttributes(FxRef<FxPrimitiveMesh<>>& mesh, cgltf_primitive* primitive);

    // static FxRef<FxAssetImage> LoadTexture(const FxRef<FxMaterial>& material, const cgltf_texture_view&
    // texture_view);

private:
    cgltf_data* mpGltfData = nullptr;

    FxSizedArray<FxMat4f> mBones;
};
