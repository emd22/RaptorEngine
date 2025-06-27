#include "FxGltfLoader.hpp"
#include "Asset/FxBaseAsset.hpp"
#include "Renderer/FxMesh.hpp"

#include <ThirdParty/cgltf.h>

#include <Asset/FxModel.hpp>

#include <Core/FxRef.hpp>

void UnpackMeshAttributes(FxMesh *mesh, cgltf_primitive *primitive)
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

    auto combined_vertices = FxMesh::MakeCombinedVertexBuffer(positions, normals, uvs);

    // Set the combined vertices to the mesh
    mesh->UploadVertices(combined_vertices);
    mesh->IsReady = true;
}

void UploadMeshToGpu(FxModel *model, cgltf_mesh *gltf_mesh, int mesh_index)
{
    for (int i = 0; i < gltf_mesh->primitives_count; i++) {
        auto *primitive = &gltf_mesh->primitives[i];

        FxSizedArray<uint32> indices;

        FxMesh *mesh = new FxMesh;

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


FxRef<FxImage> FxGltfLoader::LoadTexture(const FxRef<FxMaterial>& material, const cgltf_texture_view& texture_view)
{
    // std::cout << "Texture name: " << texture_view.texture->image->name << '\n';

    if (texture_view.texture->image->uri != nullptr) {
        std::cout << "Texture URI: " << texture_view.texture->image->uri << '\n';
        return FxRef<FxImage>(nullptr);
    }

    if (texture_view.texture->image != nullptr) {
        const uint8* data = cgltf_buffer_view_data(texture_view.texture->image->buffer_view);
        ;

        uint32 size = texture_view.texture->image->buffer_view->size;
        return FxAssetManager::LoadFromMemory<FxImage>(data, size);
    }
    else {
        std::cout << "no image added\n";
    }

    return FxRef<FxImage>(nullptr);
}

FxGltfLoader::Status FxGltfLoader::LoadFromFile(FxRef<FxBaseAsset>& asset, const std::string& path)
{
    FxModel* model = static_cast<FxModel*>(asset.Get());

    cgltf_options options{};

    cgltf_result status = cgltf_parse_file(&options, path.c_str(), &mGltfData);
    if (status != cgltf_result_success) {
        Log::Error("Error parsing GLTF file! (path: %s)", path.c_str());
        return FxGltfLoader::Status::Error;
    }

    status = cgltf_load_buffers(&options, mGltfData, path.c_str());
    if (status != cgltf_result_success) {
        Log::Error("Error loading buffers from GLTF file! (path: %s)", path.c_str());

        return FxGltfLoader::Status::Error;
    }

    std::cout << "cgltf textures; " << mGltfData->textures_count << '\n';

    if (mGltfData->materials_count) {
        for (int i = 0; i < mGltfData->materials_count; i++) {
            cgltf_material& gltf_material = mGltfData->materials[i];
            std::cout << "cgltf material: " << gltf_material.name << '\n';

            FxRef<FxMaterial> material = FxRef<FxMaterial>::New();


            if (gltf_material.has_pbr_metallic_roughness) {
                material->Diffuse = LoadTexture(material, gltf_material.pbr_metallic_roughness.base_color_texture);
            }


            model->Materials.push_back(material);
        }
    }

    return FxGltfLoader::Status::Success;
}

FxGltfLoader::Status FxGltfLoader::LoadFromMemory(FxRef<FxBaseAsset>& asset, const uint8* data, uint32 size)
{
    // (void)asset;
    FxModel* model = static_cast<FxModel*>(asset.Get());

    cgltf_options options{};


    cgltf_result status = cgltf_parse(&options, data, size, &mGltfData);
    if (status != cgltf_result_success) {
        Log::Error("Error parsing GLTF file from data");
        return FxGltfLoader::Status::Error;
    }

    std::cout << "cgltf textures; " << mGltfData->textures_count << '\n';

    if (mGltfData->materials_count) {
        for (int i = 0; i < mGltfData->materials_count; i++) {
            cgltf_material& gltf_material = mGltfData->materials[i];
            std::cout << "cgltf material: " << gltf_material.name << '\n';

            FxRef<FxMaterial> material = FxRef<FxMaterial>::New();


            if (gltf_material.has_pbr_metallic_roughness) {
                material->Diffuse = LoadTexture(material, gltf_material.pbr_metallic_roughness.base_color_texture);
            }

            model->Materials.push_back(material);
        }
    }

    return FxGltfLoader::Status::Success;
}

void FxGltfLoader::CreateGpuResource(FxRef<FxBaseAsset>& asset)
{
    FxModel *model = static_cast<FxModel *>(asset.Get());
    model->Meshes.InitSize(mGltfData->meshes_count);

    for (int i = 0; i < mGltfData->meshes_count; i++) {
        auto *mesh = &mGltfData->meshes[i];

        UploadMeshToGpu(model, mesh, i);
    }

    cgltf_free(mGltfData);
    mGltfData = nullptr;

    model->IsUploadedToGpu = true;
    model->IsUploadedToGpu.notify_all();
}

void FxGltfLoader::Destroy(FxRef<FxBaseAsset>& asset)
{
    (void)asset;

    if (mGltfData) {
        cgltf_free(mGltfData);
        mGltfData = nullptr;
    }
}
