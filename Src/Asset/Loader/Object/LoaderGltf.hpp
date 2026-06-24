#pragma once

#include "LoaderCommon.hpp"
#include "ObjectLoaderBase.hpp"

#include <Asset/Animation.hpp>
#include <Material/Material.hpp>
#include <Object/Object.hpp>
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

namespace loader {

struct AxGltfMaterialToLoad
{
    TSRef<Object> pObject { nullptr };
    int PrimitiveIndex = 0;
    int MeshIndex = 0;
};


class LoaderGltf : public ObjectLoaderBase
{
public:
    LoaderGltf() = default;

    eLoaderStatus Load(const ObjectID& id, const String& path) override;
    eLoaderStatus Load(const ObjectID& id, const uint8* data, uint32 size) override;

    void UploadMeshToGpu(TSRef<Object>& object);

    void Destroy() override;

    ~LoaderGltf() override = default;

private:
    // void MakeEmptyMaterialTexture(Ref<Material>& material, MaterialComponent& component);
    void MakeMaterialForPrimitive(Object* object, cgltf_primitive* primitive, int32 primitive_index);

    void UnpackMeshAttributes(const TSRef<Object>& object, Ref<PrimitiveMesh>& mesh, cgltf_primitive* primitive);

    int32 FindJointIndex(cgltf_skin* skin, const cgltf_node* node) const;

    void LoadSkeleton(Skeleton& skel, cgltf_skin* skin); // now takes skel by ref
    void LoadAnimation(Animation& out_anim, const cgltf_animation& anim, cgltf_skin* skin);
    void LoadAnimations(TSRef<Object>& output_object, Skeleton& skel);

    void BuildObjectsFromPrimitives(TSRef<Object>& object, cgltf_mesh* gltf_mesh);

    /**
     * @brief Process the GLTF data and build out the object tree.
     */
    void ProcessData(TSRef<Object>& output_object);

protected:
    void CreateGpuResource(const ObjectID& object_id) override;

public:
    std::vector<AxGltfMaterialToLoad> MaterialsToLoad;
    bool bKeepInMemory : 1 = false;
    SizedArray<uint32> IndexBuffer;

private:
    cgltf_data* mpGltfData = nullptr;
    String mModelPath;

    SizedArray<Mat4f> mBones;
};

} // namespace loader

} // namespace fx
