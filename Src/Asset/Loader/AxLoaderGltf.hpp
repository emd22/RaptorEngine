#pragma once

#include "AxLoaderBase.hpp"

#include <Asset/Animation.hpp>
#include <Material.hpp>
#include <Object.hpp>
#include <string>

struct cgltf_data;
struct cgltf_material;
struct cgltf_mesh;
struct cgltf_texture_view;
struct cgltf_primitive;
struct cgltf_animation;
struct cgltf_skin;
struct cgltf_node;

namespace fx {

struct AxGltfMaterialToLoad
{
    TSRef<Object> pObject { nullptr };
    int PrimitiveIndex = 0;
    int MeshIndex = 0;
};

class AxLoaderGltf : public AxLoaderBase
{
public:
    using Status = AxLoaderBase::eStatus;

    AxLoaderGltf() = default;

    Status LoadFromFile(TSRef<AxBase> asset, const std::string& path) override;
    Status LoadFromMemory(TSRef<AxBase> asset, const uint8* data, uint32 size) override;

    void UploadMeshToGpu(TSRef<Object>& object, cgltf_mesh* gltf_mesh, int mesh_index);

    void Destroy(TSRef<AxBase>& asset) override;

    ~AxLoaderGltf() override = default;

private:
    // void MakeEmptyMaterialTexture(Ref<Material>& material, MaterialComponent& component);
    void MakeMaterialForPrimitive(TSRef<Object>& object, cgltf_primitive* primitive);

    void UnpackMeshAttributes(const TSRef<Object>& object, Ref<PrimitiveMesh>& mesh, cgltf_primitive* primitive);

    int32 FindJointIndex(cgltf_skin* skin, const cgltf_node* node) const;

    void LoadSkeleton(Skeleton& skel, cgltf_skin* skin); // now takes skel by ref
    void LoadAnimation(Animation& out_anim, const cgltf_animation& anim, cgltf_skin* skin);
    void LoadAnimations(TSRef<Object>& output_object, Skeleton& skel);

public:
    std::vector<AxGltfMaterialToLoad> MaterialsToLoad;

    bool bKeepInMemory : 1 = false;

    SizedArray<uint32> IndexBuffer;

protected:
    void CreateGpuResource(TSRef<AxBase>& asset) override;

private:
    cgltf_data* mpGltfData = nullptr;

    SizedArray<Mat4f> mBones;
};

} // namespace fx
