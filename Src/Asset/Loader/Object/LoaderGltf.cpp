#include "LoaderGltf.hpp"

#include "../Image/LoaderStb.hpp"

#include <ThirdParty/cgltf.h>

#include <Asset/Animation.hpp>
#include <Asset/AssetBase.hpp>
#include <Asset/AxManager.hpp>
#include <Core/Ref.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/Object.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/PrimitiveMesh.hpp>

// Renderer includes
#include <Asset/MipmapGen.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Path.hpp>
#include <Renderer/Backend/RenderBackendFwd.hpp>
#include <Renderer/Globals.hpp>

namespace fx {

using namespace renderer;

namespace loader {

void LoaderGltf::UnpackMeshAttributes(Object* object, Ref<PrimitiveMesh>& mesh, cgltf_primitive* primitive)
{
    SizedArray<float32> positions;
    SizedArray<float32> normals;
    SizedArray<float32> uvs;
    SizedArray<float32> tangents;
    SizedArray<float32> weights;
    SizedArray<uint32> boneids;

    for (int i = 0; i < primitive->attributes_count; i++) {
        auto* attribute = &primitive->attributes[i];

        if (attribute->type == cgltf_attribute_type_position) {
            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);
            positions.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, positions.pData, data_size);
        }
        else if (attribute->type == cgltf_attribute_type_normal) {
            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);
            normals.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, normals.pData, data_size);
        }
        else if (attribute->type == cgltf_attribute_type_texcoord) {
            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);
            uvs.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, uvs.pData, data_size);
        }
        else if (attribute->type == cgltf_attribute_type_tangent) {
            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);
            tangents.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, tangents.pData, data_size);
        }
        else if (attribute->type == cgltf_attribute_type_weights) {
            cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);
            weights.InitSize(data_size);
            cgltf_accessor_unpack_floats(attribute->data, weights.pData, data_size);
        }
        else if (attribute->type == cgltf_attribute_type_joints) {
            boneids.InitSize(attribute->data->count * 4);
            for (cgltf_size j = 0; j < attribute->data->count; j++) {
                cgltf_accessor_read_uint(attribute->data, j, reinterpret_cast<cgltf_uint*>(&boneids.pData[j * 4]), 4);
            }
        }
    }

    // Since GLTF is stored with right handed coordinates (-x, y, z), we need to flip X when creating the vertex
    // buffers.
    constexpr eVertexCreateFlags create_flags = eVertexCreateFlags::NegativeX;
    mesh->VertexList.CreateFrom(positions, normals, uvs, tangents, weights, boneids, create_flags);

    // mesh->UploadVertices();
}

// static void GenerateMipsForTexture(const String& asset_path, const Slice<uint8>& pixels)
// {
//     MipmapGen mg {};

//     const String path = String::Fmt("");

//     mg.GenerateMipmaps(path.CStr(), eImageFormat::RGBA8_UNorm, pixels, )
// }

// template <eImageFormat TFormat>
// static void LoadMipmapsIfExists(const String& asset_path, const char* component_name,
//                                 MaterialComponent<TFormat>& component)
// {
//     // Get the folder that the model would be stored in.
//     // This uses the normal path minus the file extension.
//     // For example,
//     //      Assets/Folder/NameOfModel.glb   becomes    Assets/Folder/NameOfModel/...

//     // const FilePath base_path = FilePath(asset_path).RemoveExtension();

//     Path base_path = Path(asset_path);
//     base_path.RemoveExtension();
//     base_path.Add(String(component_name) + ".ftx");


//     // Get the full path:
//     //      Assets/Folder/NameOfModel/Diffuse.ftx

//     const String full_path = String::Fmt("{}/{}.ftx", base_path.Str(), component_name);

//     // The pregenerated file exists, use it.
//     if (FilesystemIO::FileExists(full_path.CStr())) {
//         // TSRef<AxImage> image = TSRef<AxImage>::New();

//         // image->Image.CreateFromData(eImageType::Flat, const Vec2u &size, uint16 mips_count, eImageFormat format,
//         // const SizedArray<uint8> &image_data, eImageCreateFlags flags)

//         // component.pAssetImage = image;
//         // component.pDataToLoad = nullptr;
//     }
//     else {
//         // MipmapGen mm {};
//         // mm.GenerateMipmaps(full_path.CStr(), eImageFormat::RGBA8_UNorm, const Slice<uint8>& pixels,
//         //                    const Vec2u& size)
//     }
// }


static String MakeMaterialTextureCacheName(Material* material, const char* component_name)
{
    return String::Fmt("{}_{}.ftx", material->Name.Get(), component_name);
}


static String GetTextureCachePath(const String& asset_path, Material* material, const char* component_name)
{
    Path path = Path(asset_path);
    // Transform the basename into a directory
    // Some/Path/Filename.txt   to   Some/Path/Filename/
    path.RemoveExtension();
    // Ensure that the directories for the path are created
    path.CreateDirs();

    // Add the filename
    path.Add(String::Fmt("{}_{}.ftx", material->Name.Get(), component_name));

    return std::move(path.Str());
}


static void GenerateMipmapImage(const String& output_path, eImageFormat format, const uint8* data, uint32 size)
{
    TSRef<loader::LoaderStb> loader = TSRef<loader::LoaderStb>::New();
    loader->ImageType = eImageType::Flat;
    loader->ImageFormat = format;
    loader->CreationFlags = eImageCreateFlags::None;

    TSRef<AxImage> image = TSRef<AxImage>::New();
    loader::eLoaderStatus status = loader->Load(image, data, size);

    if (status != loader::eLoaderStatus::Success) {
        LogError("Error loading material texture!");
        return;
    }

    MipmapGen mm {};
    mm.GenerateMipmaps(output_path.CStr(), format, loader->GetImageData(), loader->GetImageSize());

    // loader.InvalidateImageData();
}


template <eImageFormat TFormat>
static void MakeMaterialTextureForPrimitive(const String& asset_path, Material* material, const char* component_name,
                                            MaterialComponent<TFormat>& component, cgltf_texture_view& texture_view)
{
    Assert(texture_view.texture != nullptr);

    // FilePath tex_cache_path = FilePath(String::Fmt("{}_{}", FilePath(asset_path).RemoveExtension().Str(),
    //                                                MakeMaterialTextureCacheName(material, component_name)));

    String tex_cache_path = GetTextureCachePath(asset_path, material, component_name);


    const bool texture_cache_exists = FilesystemIO::FileExists(tex_cache_path);

    if (texture_cache_exists) {
        MipmapLoader ml {};

        ml.Open(tex_cache_path.CStr());

        component.UploadSrc = eMaterialComponentUploadSrc::DirectUpload;
        component.ImageToUpload = ml.GetMip(4);
        return;
    }

    const uint8* image_buffer = cgltf_buffer_view_data(texture_view.texture->image->buffer_view);
    uint32 image_buffer_size = static_cast<uint32>(texture_view.texture->image->buffer_view->size);

    // Stage that shit so we can nuke mGltfData as soon as we can
    uint8* goober_buffer = static_cast<uint8*>(std::malloc(image_buffer_size));
    memcpy(goober_buffer, image_buffer, image_buffer_size);

    Assert(goober_buffer != nullptr);
    Assert(image_buffer_size > 0);

    // Submit as data to be loaded later by the asset manager
    component.UploadSrc = eMaterialComponentUploadSrc::ProcessAndUpload;
    component.pDataToLoad = MakeSlice(const_cast<const uint8*>(goober_buffer), image_buffer_size);

    if (!texture_cache_exists) {
        GenerateMipmapImage(tex_cache_path, TFormat, goober_buffer, image_buffer_size);
    }
}

void LoaderGltf::MakeMaterialForPrimitive(Object* object, cgltf_primitive* primitive, int32 primitive_index)
{
    // if (!object->mMaterialID.IsNull()) {
    //     return;
    // }

    cgltf_material* gltf_material = primitive->material;

    if (!gltf_material) {
        return;
    }

    String material_name = (gltf_material->name) ? gltf_material->name : object->Name.Get();


    object->mMaterialID = gMaterialManager->NewMaterial(material_name, ePipelineName::Geometry, object->IsSkinned());

    Material* material = gMaterialManager->GetMaterial(object->mMaterialID);

    // For some reason the peeber metallic roughness holds our diffuse texture
    if (gltf_material->has_pbr_metallic_roughness) {
        cgltf_texture_view& texture_view = gltf_material->pbr_metallic_roughness.base_color_texture;

        if (!texture_view.texture) {
            // MakeEmptyMaterialTexture(material, material->Diffuse);
            material->Diffuse.pAssetImage = AxImage::GetEmptyImage<eImageFormat::RGBA8_UNorm>();

            // material->Properties.BaseColor =
            // Color::FromFloats(gltf_material->pbr_metallic_roughness.base_color_factor);
        }
        else {
            MakeMaterialTextureForPrimitive(mModelPath, material, "AL", material->Diffuse, texture_view);
            // material->Properties.BaseColor = Color::FromRGBA(1, 1, 1, 255);
        }
    }
    else {
        // There is no albedo texture on the model, use the base colour.
        // material->Properties.BaseColor = Color::FromFloats(gltf_material->pbr_metallic_roughness.base_color_factor);
    }

    // Load the normalmap
    if (gltf_material->normal_texture.texture != nullptr) {
        MakeMaterialTextureForPrimitive(mModelPath, material, "NM", material->NormalMap, gltf_material->normal_texture);
    }

    // Load the metallic/roughness texture
    if (gltf_material->pbr_metallic_roughness.metallic_roughness_texture.texture != nullptr) {
        MakeMaterialTextureForPrimitive(mModelPath, material, "MR", material->MetallicRoughness,
                                        gltf_material->pbr_metallic_roughness.metallic_roughness_texture);
    }

    material->SetDefaultPipeline();
    material->bReadyToCheck.test_and_set();
}

void LoaderGltf::BuildObjectsFromPrimitives(const ObjectID& container_id, cgltf_mesh* gltf_mesh)
{
    const bool has_multiple_primitives = gltf_mesh->primitives_count > 1;

    // Assume at first that there is only one primitive;
    ObjectID current_id = container_id;

    Object* container_object = gObjectManager->GetObject(container_id);

    // Similarly to `CreateGpuResource`, we are going to make the `object` into a container
    // if there are multiple primitives.
    if (has_multiple_primitives) {
        current_id = gObjectManager->NewObjectID(gObjectManager->GetObject(container_id)->Name.Get());
    }

    for (int i = 0; i < gltf_mesh->primitives_count; i++) {
        cgltf_primitive* gltf_primitive = &gltf_mesh->primitives[i];
        Ref<PrimitiveMesh> primitive_mesh = Ref<PrimitiveMesh>::New();

        SizedArray<uint32> indices;

        // Keep the primitive mesh's vertices and indices in memory if `KeepInMemory` is set
        primitive_mesh->bKeepInMemory = bKeepInMemory;

        // Load the indices in from the mesh
        if (gltf_primitive->indices != nullptr) {
            indices.InitSize(gltf_primitive->indices->count);
            cgltf_accessor_unpack_indices(gltf_primitive->indices, indices.pData, sizeof(uint32),
                                          gltf_primitive->indices->count);
            primitive_mesh->SetIndices(std::move(indices));
        }

        Object* current_object = gObjectManager->GetObject(current_id);

        UnpackMeshAttributes(current_object, primitive_mesh, gltf_primitive);
        current_object->pMesh = primitive_mesh;

        MakeMaterialForPrimitive(current_object, gltf_primitive, i);

        if (has_multiple_primitives) {
            // Attach the current object to the object container (our output)
            container_object->AttachObject(current_id);

            // Create a new object to load into next
            current_id = gObjectManager->NewObjectID(container_object->Name.Get());
        }
    }
}


void LoaderGltf::UploadMeshToGpu(Object* object)
{
    // uint32 attached_count = object->AttachedNodes.Size();

    // for (int32 i = 0; i < attached_count; i++) {
    //     Ref<PrimitiveMesh> primitive_mesh = object->AttachedNodes[i]->pMesh;

    //     // Set the mesh indices
    //     primitive_mesh->UploadIndices();
    //     primitive_mesh->UploadVertices();

    //     primitive_mesh->bIsReady = true;
    // }


    if (object->pMesh.IsValid()) {
        Ref<PrimitiveMesh> primitive_mesh = object->pMesh;

        LogInfo(LC_ASSET, "Upload mesh to GPU");

        // Set the mesh indices
        primitive_mesh->UploadIndices(RenderBackendFwd::GetUploadCmd());
        primitive_mesh->UploadVertices(RenderBackendFwd::GetUploadCmd());

        primitive_mesh->bIsReady = true;
    }
}


int32 LoaderGltf::FindJointIndex(cgltf_skin* skin, const cgltf_node* node) const
{
    for (cgltf_size i = 0; i < skin->joints_count; i++) {
        if (skin->joints[i] == node) {
            return static_cast<int32>(i);
        }
    }

    return BoneNull;
}


void LoaderGltf::LoadSkeleton(Skeleton& skel, cgltf_skin* skin)
{
    if (!skin) {
        return;
    }

    const uint32 joint_count = static_cast<uint32>(skin->joints_count);
    skel.JointCount = joint_count;

    // Inverse bind matrices
    if (skin->inverse_bind_matrices) {
        cgltf_accessor* accessor = skin->inverse_bind_matrices;
        Assert(accessor->count == joint_count);

        skel.InvBindTransforms.InitSize(joint_count);
        cgltf_accessor_unpack_floats(accessor, reinterpret_cast<float32*>(skel.InvBindTransforms.pData),
                                     joint_count * 16);

        // The GLTF coordinate system is garbage, reflect so -X becomes X (this also changes rotation back from CCW to
        // CW).
        Mat4f reflection = Mat4f::sIdentity;
        reflection.Columns[0].X = -1.0f;

        for (uint32 i = 0; i < joint_count; i++) {
            Mat4f& m = skel.InvBindTransforms[i];

            m = reflection * m * reflection;
        }
    }

    // Parent indices and names
    skel.ParentIndices.InitSize(joint_count);
    skel.BoneNames.InitSize(joint_count);
    skel.LocalTransforms.InitSize(joint_count);
    skel.WorldTransforms.InitSize(joint_count);
    skel.SkinningMatrices.InitSize(joint_count);

    for (uint32 i = 0; i < joint_count; i++) {
        const cgltf_node* joint = skin->joints[i];

        skel.BoneNames[i] = joint->name ? String(joint->name) : String::Fmt("joint_{}", i);
        skel.ParentIndices[i] = joint->parent ? FindJointIndex(skin, joint->parent) : BoneNull;
    }
}

void LoaderGltf::LoadAnimation(Animation& out_anim, const cgltf_animation& anim, cgltf_skin* skin)
{
    const uint32 joint_count = skin ? static_cast<uint32>(skin->joints_count) : 0;

    out_anim.Name = anim.name ? anim.name : "Unnamed";
    out_anim.Duration = 0.0f;
    out_anim.BoneTracks.InitSize(joint_count);

    for (uint32 i = 0; i < joint_count; i++) {
        out_anim.BoneTracks.pData[i] = BoneTrack {};
    }

    for (cgltf_size ch = 0; ch < anim.channels_count; ch++) {
        const cgltf_animation_channel* channel = &anim.channels[ch];
        const cgltf_animation_sampler* sampler = channel->sampler;

        if (!skin || !channel->target_node) {
            continue;
        }

        const int32 joint_idx = FindJointIndex(skin, channel->target_node);

        if (joint_idx < 0) {
            continue;
        }

        BoneTrack& joint_track = out_anim.BoneTracks.pData[joint_idx];

        const cgltf_size key_count = sampler->input->count;
        const cgltf_size num_components = cgltf_num_components(sampler->output->type);

        SizedArray<float32> times;
        times.InitSize(key_count);
        cgltf_accessor_unpack_floats(sampler->input, times.pData, key_count);

        if (key_count > 0) {
            out_anim.Duration = std::max(out_anim.Duration, times.pData[key_count - 1]);
        }

        // Get translations
        if (channel->target_path == cgltf_animation_path_type_translation) {
            Assert(sampler->output->type == cgltf_type_vec3);
            joint_track.Translation.Times = std::move(times);
            joint_track.Translation.Values.InitSize(key_count);

            for (uint32 key_index = 0; key_index < key_count; key_index++) {
                float32 buffer[4];

                cgltf_accessor_read_float(sampler->output, key_index, buffer, 3);

                buffer[0] = -buffer[0];
                buffer[3] = 0.0f;

                joint_track.Translation.Values[key_index] = (Vec3f(buffer));
            }
        }

        // Get rotations
        else if (channel->target_path == cgltf_animation_path_type_rotation) {
            Assert(sampler->output->type == cgltf_type_vec4);
            joint_track.Rotation.Times = std::move(times);
            joint_track.Rotation.Values.InitSize(key_count);

            for (uint32 key_index = 0; key_index < key_count; key_index++) {
                float32 buffer[4];

                cgltf_accessor_read_float(sampler->output, key_index, buffer, 4);
                // buffer[0] = -buffer[0];
                buffer[1] = -buffer[1];
                buffer[2] = -buffer[2];

                joint_track.Rotation.Values[key_index] = Quat(buffer);
            }
        }
    }

    LogInfo(LC_ASSET, "Loaded animation '{}': {:.3f}s, {} joints", out_anim.Name, out_anim.Duration, joint_count);
}

void LoaderGltf::LoadAnimations(Object* object, Skeleton& skel)
{
    if (!mpGltfData->animations_count || mpGltfData->skins_count == 0) {
        return;
    }

    // Most models only have one skin/skeleton, so we just assume one for now.
    cgltf_skin* skin = &mpGltfData->skins[0];

    object->Animations.InitCapacity(8);


    for (uint32 i = 0; i < mpGltfData->animations_count; i++) {
        Animation anim;
        LoadAnimation(anim, mpGltfData->animations[i], skin);
        object->Animations.Insert(std::move(anim));
    }

    LogInfo(LC_ASSET, "Loaded {} animations", mpGltfData->animations_count);
}

void LoaderGltf::ProcessData(const ObjectID& output_id)
{
    // If there is only one mesh to load, store the mesh directly in the output object
    ObjectID current_id = output_id;

    Object* output_object = gObjectManager->GetObject(output_id);

    // If there are multiple gltf meshes, we will need to use the output object as a
    // container for multiple other meshes
    const bool has_multiple_meshes = mpGltfData->meshes_count > 1;

    int32 attach_id = 1;

    // If there are multiple objects, each object found will be attached to the output object.
    if (has_multiple_meshes) {
        current_id = gObjectManager->NewObjectID(std::format("{}_{}", output_object->Name.Get(), attach_id++));
    }


    // Load each object in the GLTF as a new separate object.
    for (int32 node_index = 0; node_index < mpGltfData->nodes_count; node_index++) {
        cgltf_node* node = &mpGltfData->nodes[node_index];

        Object* current_object = gObjectManager->GetObject(current_id);

        if (node->mesh) {
            // Load all meshes from the node we are currently on.
            BuildObjectsFromPrimitives(current_id, node->mesh);

            // If the node has a skeleton, load it in.
            if (node->skin) {
                current_object->pSkeleton = Ref<Skeleton>::New();
                LoadSkeleton(*current_object->pSkeleton, node->skin);
                LoadAnimations(current_object, *current_object->pSkeleton);
            }
        }

        if (has_multiple_meshes) {
            // Attach the loaded object onto the final object. This will be a container.
            output_object->AttachObject(current_id);

            // Create a new object to load into next
            current_id = gObjectManager->NewObjectID(std::format("{}_{}", output_object->Name.Get(), attach_id++));
        }
    }
}

eLoaderStatus LoaderGltf::Load(const ObjectID& object_id, const String& path)
{
    cgltf_options options {};

    mModelPath = path;

    cgltf_result status = cgltf_parse_file(&options, path.CStr(), &mpGltfData);
    if (status != cgltf_result_success) {
        LogError(LC_ASSET, "Error parsing GLTF file! (path: {})", path);
        return eLoaderStatus::Error;
    }

    status = cgltf_load_buffers(&options, mpGltfData, path.CStr());
    if (status != cgltf_result_success) {
        LogError(LC_ASSET, "Error loading buffers from GLTF file! (path: {:s})", path);
        return eLoaderStatus::Error;
    }

    ProcessData(object_id);

    return eLoaderStatus::Success;
}


eLoaderStatus LoaderGltf::Load(const ObjectID& object_id, const uint8* data, uint32 size)
{
    cgltf_options options {};

    cgltf_result status = cgltf_parse(&options, data, size, &mpGltfData);
    if (status != cgltf_result_success) {
        LogError(LC_ASSET, "Error parsing GLTF file from data");
        return eLoaderStatus::Error;
    }

    ProcessData(object_id);

    return eLoaderStatus::Success;
}

void LoaderGltf::CreateGpuResource(const ObjectID& object_id)
{
    Object* container_object = gObjectManager->GetObject(object_id);

    // If there is only one mesh to load, store the mesh directly in the output object
    // TSRef<Object> current_object = container_object;

    // If there are multiple gltf meshes, we will need to use the output object as a
    // container for multiple other meshes
    // const bool has_multiple_meshes = mpGltfData->meshes_count > 1;

    // UploadMeshToGpu(container_object);

    uint32 attached_count = container_object->AttachedNodes.Size();

    // container_object->PrintDebug();

    for (uint32 i = 0; i < attached_count; i++) {
        UploadMeshToGpu(gObjectManager->GetObject(container_object->AttachedNodes[i]));
    }

    UploadMeshToGpu(container_object);


    // asset->bIsUploadedToGpu = true;
    // asset->bIsUploadedToGpu.notify_all();
}

void LoaderGltf::Destroy()
{
    if (mpGltfData) {
        cgltf_free(mpGltfData);
        mpGltfData = nullptr;
    }
}

} // namespace loader

} // namespace fx
