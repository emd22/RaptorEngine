#include "AxLoaderGltf.hpp"

#include <ThirdParty/cgltf.h>

#include <Asset/AxBase.hpp>
#include <Asset/AxManager.hpp>
#include <Core/FxRef.hpp>
#include <FxEngine.hpp>
#include <FxMaterial.hpp>
#include <FxObject.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>
#include <Renderer/RxRenderBackend.hpp>
#include <iostream>

FxRef<AxImage> LoadTexture(const FxRef<FxMaterial>& material, const cgltf_texture_view& texture_view);

void AxLoaderGltf::UnpackMeshAttributes(const FxRef<FxObject>& object, FxRef<FxPrimitiveMesh<>>& mesh,
                                        cgltf_primitive* primitive)
{
    FxSizedArray<float32> positions;
    FxSizedArray<float32> normals;
    FxSizedArray<float32> uvs;
    FxSizedArray<float32> tangents;

    for (int i = 0; i < primitive->attributes_count; i++) {
        auto* attribute = &primitive->attributes[i];

        if (attribute->type == cgltf_attribute_type_position) {
            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);

            // Create our vertex position buffer
            positions.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, positions.pData, data_size);
        }
        else if (attribute->type == cgltf_attribute_type_normal) {
            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);

            // Create our vertex normal buffer
            normals.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, normals.pData, data_size);
        }
        else if (attribute->type == cgltf_attribute_type_texcoord) {
            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);

            // Create our vertex normal buffer
            uvs.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, uvs.pData, data_size);
        }
        else if (attribute->type == cgltf_attribute_type_tangent) {
            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);

            FxLogInfo("Model contains tangents!");
            // Create our vertex normal buffer
            tangents.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, tangents.pData, data_size);
        }
    }

    mesh->VertexList.CreateFrom(positions, normals, uvs, tangents);
    mesh->UploadVertices();

    mesh->IsReady = true;
}

void AxLoaderGltf::LoadAnimationSkin(FxRef<FxPrimitiveMesh<>>& mesh, cgltf_skin* skin)
{
    FxLogInfo("Joints: {}", skin->joints_count);
}

template <RxImageFormat TFormat>
static void MakeMaterialTextureForPrimitive(FxRef<FxMaterial>& material, FxMaterialComponent<TFormat>& component,
                                            cgltf_texture_view& texture_view)
{
    if (!texture_view.texture) {
        return;
    }

    const uint8* image_buffer = cgltf_buffer_view_data(texture_view.texture->image->buffer_view);
    uint32 image_buffer_size = static_cast<uint32>(texture_view.texture->image->buffer_view->size);

    // Stage that shit so we can nuke mGltfData as soon as we can
    uint8* goober_buffer = FxMemPool::Alloc<uint8>(image_buffer_size);
    memcpy(goober_buffer, image_buffer, image_buffer_size);

    // Submit as data to be loaded later by the asset manager
    component.pDataToLoad = FxMakeSlice(const_cast<const uint8*>(goober_buffer), image_buffer_size);
}

void AxLoaderGltf::MakeMaterialForPrimitive(FxRef<FxObject>& object, cgltf_primitive* primitive)
{
    cgltf_material* gltf_material = primitive->material;

    if (!gltf_material) {
        return;
    }

    FxRef<FxMaterial> material = FxMaterialManager::New("Fireplace", &gRenderer->pDeferredRenderer->PlGeometry);

    // For some reason the peeber metallic roughness holds our diffuse texture
    if (gltf_material->has_pbr_metallic_roughness) {
        cgltf_texture_view& texture_view = gltf_material->pbr_metallic_roughness.base_color_texture;

        if (!texture_view.texture) {
            // MakeEmptyMaterialTexture(material, material->Diffuse);
            material->Diffuse.pImage = AxImage::GetEmptyImage<RxImageFormat::eRGBA8_UNorm>();

            material->Properties.BaseColor = FxColor::FromFloats(
                gltf_material->pbr_metallic_roughness.base_color_factor);
        }
        else {
            MakeMaterialTextureForPrimitive(material, material->Diffuse, texture_view);
            material->Properties.BaseColor = FxColor::FromRGBA(1, 1, 1, 255);
        }
    }

    // There is no albedo texture on the model, use the base colour.
    else {
        material->Properties.BaseColor = FxColor::FromFloats(gltf_material->pbr_metallic_roughness.base_color_factor);
    }

    if (gltf_material->normal_texture.texture != nullptr) {
        MakeMaterialTextureForPrimitive(material, material->NormalMap, gltf_material->normal_texture);
    }
    else {
        material->NormalMap.pImage = nullptr;
    }


    if (gltf_material->pbr_metallic_roughness.metallic_roughness_texture.texture != nullptr) {
        MakeMaterialTextureForPrimitive(material, material->MetallicRoughness,
                                        gltf_material->pbr_metallic_roughness.metallic_roughness_texture);
    }
    else {
        // material->MetallicRoughness.pImage = nullptr;

        material->MetallicRoughness.pImage = AxImage::GetEmptyImage<RxImageFormat::eRGBA8_UNorm>();
    }


    object->pMaterial = material;
}

void AxLoaderGltf::UploadMeshToGpu(FxRef<FxObject>& object, cgltf_mesh* gltf_mesh, int mesh_index)
{
    const bool has_multiple_primitives = gltf_mesh->primitives_count > 1;

    // Assume at first that there is only one primitive;
    FxRef<FxObject> current_object = object;

    // Similarly to `CreateGpuResource`, we are going to make the `object` into a container
    // if there are multiple primitives.
    if (has_multiple_primitives) {
        current_object = FxMakeRef<FxObject>();
    }

    for (int i = 0; i < gltf_mesh->primitives_count; i++) {
        auto* primitive = &gltf_mesh->primitives[i];

        FxSizedArray<uint32> indices;

        FxRef<FxPrimitiveMesh<>> primitive_mesh = FxMakeRef<FxPrimitiveMesh<>>();

        // Keep the primitive mesh's vertices and indices in memory if `KeepInMemory` is set
        primitive_mesh->KeepInMemory = bKeepInMemory;

        // if there are indices in the mesh, add them to the FxPrimitiveMesh
        if (primitive->indices != nullptr) {
            indices.InitSize(primitive->indices->count);

            cgltf_accessor_unpack_indices(primitive->indices, indices.pData, sizeof(uint32), primitive->indices->count);

            // Set the mesh indices
            primitive_mesh->UploadIndices(std::move(indices));
        }

        UnpackMeshAttributes(current_object, primitive_mesh, primitive);

        MakeMaterialForPrimitive(current_object, primitive);

        current_object->pMesh = primitive_mesh;

        if (has_multiple_primitives) {
            // Attach the current object to the object container (our output)
            object->AttachObject(current_object);

            // Create a new object to load into next
            current_object = FxMakeRef<FxObject>();
        }
    }

    //    object = current_object;
}

void AxLoaderGltf::LoadAnimations() { FxLogInfo("Gltf model has {} animations", mpGltfData->animations_count); }


AxLoaderGltf::Status AxLoaderGltf::LoadFromFile(FxRef<AxBase> asset, const std::string& path)
{
    cgltf_options options {};

    cgltf_result status = cgltf_parse_file(&options, path.c_str(), &mpGltfData);
    if (status != cgltf_result_success) {
        FxLogError("Error parsing GLTF file! (path: {:s})", path);
        return AxLoaderGltf::Status::eError;
    }

    status = cgltf_load_buffers(&options, mpGltfData, path.c_str());
    if (status != cgltf_result_success) {
        FxLogError("Error loading buffers from GLTF file! (path: {:s})", path);

        return AxLoaderGltf::Status::eError;
    }


    return AxLoaderGltf::Status::eSuccess;
}

AxLoaderGltf::Status AxLoaderGltf::LoadFromMemory(FxRef<AxBase> asset, const uint8* data, uint32 size)
{
    // (void)asset;
    //    FxRef<FxAssetModel> model(asset);

    cgltf_options options {};

    cgltf_result status = cgltf_parse(&options, data, size, &mpGltfData);
    if (status != cgltf_result_success) {
        FxLogError("Error parsing GLTF file from data");
        return AxLoaderGltf::Status::eError;
    }

    return AxLoaderGltf::Status::eSuccess;
}

void AxLoaderGltf::CreateGpuResource(FxRef<AxBase>& asset)
{
    FxRef<FxObject> output_object(asset);


    // If there is only one mesh to load, store the mesh directly in the output object
    FxRef<FxObject> current_object = output_object;

    // If there are multiple gltf meshes, we will need to use the output object as a
    // container for multiple other meshes
    const bool has_multiple_meshes = mpGltfData->meshes_count > 1;
    if (has_multiple_meshes) {
        current_object = FxMakeRef<FxObject>();
    }

    FxLogInfo("Unpacking GLTF object with {} meshes", mpGltfData->meshes_count);

    for (int32 node_index = 0; node_index < mpGltfData->nodes_count; node_index++) {
        cgltf_node* node = &mpGltfData->nodes[node_index];

        // Might be good to not ignore the other GLTF things in the future, but thats not a priority for now
        if (!node->mesh) {
            continue;
        }

        UploadMeshToGpu(current_object, node->mesh, node_index);

        if (node->skin) {
            LoadAnimationSkin(current_object->pMesh, node->skin);
        }

        if (has_multiple_meshes) {
            // Attach the current object to the object container (our output)
            output_object->AttachObject(current_object);

            // Create a new object to load into next
            current_object = FxMakeRef<FxObject>();
        }
    }

    asset = output_object;

    asset->bIsUploadedToGpu = true;
    asset->bIsUploadedToGpu.notify_all();
}

void AxLoaderGltf::Destroy(FxRef<AxBase>& asset)
{
    if (mpGltfData) {
        printf("Destroyed mesh\n");
        cgltf_free(mpGltfData);
        mpGltfData = nullptr;
    }
}
