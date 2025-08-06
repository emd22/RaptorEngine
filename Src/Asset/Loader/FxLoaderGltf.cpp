#include "FxLoaderGltf.hpp"
#include "Asset/FxAssetBase.hpp"
#include "Renderer/FxMesh.hpp"

#include <ThirdParty/cgltf.h>

#include <Asset/FxAssetModel.hpp>

#include <Core/FxRef.hpp>

void UnpackMeshAttributes(FxMesh<>* mesh, cgltf_primitive* primitive)
{
    FxSizedArray<float32> positions;
    FxSizedArray<float32> normals;
    FxSizedArray<float32> uvs;

    for (int i = 0; i < primitive->attributes_count; i++) {
        auto *attribute = &primitive->attributes[i];


        if (attribute->type == cgltf_attribute_type_position) {
            printf("Attribute -> Vertex Buffer\n");

            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);

            // Create our vertex position buffer
            positions.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, positions.Data, data_size);
        }
        else if (attribute->type == cgltf_attribute_type_normal) {
            printf("Attribute -> Normal Buffer\n");

            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);

            // Create our vertex normal buffer
            normals.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, normals.Data, data_size);
        }
        else if (attribute->type == cgltf_attribute_type_texcoord) {
            printf("Attribute -> UV Buffer\n");

            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);

            // Create our vertex normal buffer
            uvs.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, uvs.Data, data_size);
        }
    }

    auto combined_vertices = FxMesh<>::MakeCombinedVertexBuffer(positions, normals, uvs);

    // Set the combined vertices to the mesh
    mesh->UploadVertices(combined_vertices);
    mesh->IsReady = true;
}

void UploadMeshToGpu(FxRef<FxAssetModel>& model, cgltf_mesh *gltf_mesh, int mesh_index)
{
    for (int i = 0; i < gltf_mesh->primitives_count; i++) {
        auto* primitive = &gltf_mesh->primitives[i];

        FxSizedArray<uint32> indices;

        FxMesh<>* mesh = new FxMesh;

        // if there are indices in the mesh, add them to the FxMesh
        if (primitive->indices != nullptr) {
            indices.InitSize(primitive->indices->count);

            cgltf_accessor_unpack_indices(
                primitive->indices,
                indices.Data,
                sizeof(uint32),
                primitive->indices->count
            );

            // Set the mesh indices
            mesh->UploadIndices(indices);
        }

        UnpackMeshAttributes(mesh, primitive);

        model->Meshes.Data[i] = mesh;
    }
    Log::Info("Add mesh:", 0);

}

#include <FxMaterial.hpp>
#include <iostream>

#include <Asset/FxAssetManager.hpp>


FxRef<FxAssetImage> FxLoaderGltf::LoadTexture(const FxRef<FxMaterial>& material, const cgltf_texture_view& texture_view)
{
    // std::cout << "Texture name: " << texture_view.texture->image->name << '\n';

    if (texture_view.texture->image->uri != nullptr) {
        std::cout << "Texture URI: " << texture_view.texture->image->uri << '\n';
        return FxRef<FxAssetImage>(nullptr);
    }

    if (texture_view.texture->image != nullptr) {
        const uint8* data = cgltf_buffer_view_data(texture_view.texture->image->buffer_view);

        uint32 size = texture_view.texture->image->buffer_view->size;
        FxRef<FxAssetImage> texture = FxAssetManager::LoadFromMemory<FxAssetImage>(data, size);
//        texture->WaitUntilLoaded();
        return texture;
    }
    else {
        std::cout << "no image added\n";
    }

    return FxRef<FxAssetImage>(nullptr);
}

FxLoaderGltf::Status FxLoaderGltf::LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path)
{
    FxRef<FxAssetModel> model(asset);

    cgltf_options options{};

    cgltf_result status = cgltf_parse_file(&options, path.c_str(), &mGltfData);
    if (status != cgltf_result_success) {
        Log::Error("Error parsing GLTF file! (path: %s)", path.c_str());
        return FxLoaderGltf::Status::Error;
    }

    status = cgltf_load_buffers(&options, mGltfData, path.c_str());
    if (status != cgltf_result_success) {
        Log::Error("Error loading buffers from GLTF file! (path: %s)", path.c_str());

        return FxLoaderGltf::Status::Error;
    }

    std::cout << "cgltf textures; " << mGltfData->textures_count << '\n';

    if (mGltfData->materials_count) {
        for (int i = 0; i < mGltfData->materials_count; i++) {
            cgltf_material& gltf_material = mGltfData->materials[i];
            std::cout << "cgltf material: " << gltf_material.name << '\n';

            FxRef<FxMaterial> material = FxRef<FxMaterial>::New();


            if (gltf_material.has_pbr_metallic_roughness) {
                material->DiffuseTexture = LoadTexture(material, gltf_material.pbr_metallic_roughness.base_color_texture);
                
            }


            model->Materials.push_back(material);
        }
    }

    return FxLoaderGltf::Status::Success;
}

FxLoaderGltf::Status FxLoaderGltf::LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size)
{
    // (void)asset;
    FxRef<FxAssetModel> model(asset);

    cgltf_options options{};

    cgltf_result status = cgltf_parse(&options, data, size, &mGltfData);
    if (status != cgltf_result_success) {
        Log::Error("Error parsing GLTF file from data");
        return FxLoaderGltf::Status::Error;
    }

    std::cout << "cgltf textures; " << mGltfData->textures_count << '\n';

    if (mGltfData->materials_count) {
        for (int i = 0; i < mGltfData->materials_count; i++) {
            cgltf_material& gltf_material = mGltfData->materials[i];
            std::cout << "cgltf material: " << gltf_material.name << '\n';

            FxRef<FxMaterial> material = FxRef<FxMaterial>::New();


            if (gltf_material.has_pbr_metallic_roughness) {
                material->DiffuseTexture = LoadTexture(material, gltf_material.pbr_metallic_roughness.base_color_texture);
            }

            model->Materials.push_back(material);
        }
    }

    return FxLoaderGltf::Status::Success;
}

void FxLoaderGltf::CreateGpuResource(FxRef<FxAssetBase>& asset)
{
    FxRef<FxAssetModel> model(asset);

    model->Meshes.InitSize(mGltfData->meshes_count);

    for (int i = 0; i < mGltfData->meshes_count; i++) {
        auto *mesh = &mGltfData->meshes[i];

        UploadMeshToGpu(model, mesh, i);
    }

//    cgltf_free(mGltfData);
//    mGltfData = nullptr;

    model->IsUploadedToGpu = true;
    model->IsUploadedToGpu.notify_all();
}

void FxLoaderGltf::Destroy(FxRef<FxAssetBase>& asset)
{
//    while (!asset->IsUploadedToGpu) {
//        asset->IsUploadedToGpu.wait(true);
//    }

    if (mGltfData) {
       printf("Destroyed mesh\n");
       cgltf_free(mGltfData);
       mGltfData = nullptr;
    }
}
