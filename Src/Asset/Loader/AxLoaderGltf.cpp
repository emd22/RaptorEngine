#include "AxLoaderGltf.hpp"

#include <ThirdParty/cgltf.h>

#include <Asset/AxBase.hpp>
#include <Asset/AxManager.hpp>
#include <Asset/FxAnimation.hpp>
#include <Core/FxRef.hpp>
#include <FxMaterial.hpp>
#include <FxObject.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>

// Renderer includes
#include <Renderer/RxGlobals.hpp>
#include <Renderer/RxRenderBackend.hpp>

FxTSRef<AxImage> LoadTexture(const FxTSRef<FxMaterial>& material, const cgltf_texture_view& texture_view);

void AxLoaderGltf::UnpackMeshAttributes(const FxTSRef<FxObject>& object, FxRef<FxPrimitiveMesh>& mesh,
                                        cgltf_primitive* primitive)
{
    FxSizedArray<float32> positions;
    FxSizedArray<float32> normals;
    FxSizedArray<float32> uvs;
    FxSizedArray<float32> tangents;
    FxSizedArray<float32> weights;
    FxSizedArray<uint32> boneids;

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

    uint32 max_bone_id = 0;
    uint32 min_bone_id = 0;
    for (uint32 bone_id : boneids) {
        max_bone_id = std::max(max_bone_id, bone_id);
        min_bone_id = std::min(min_bone_id, bone_id);
    }

    if (max_bone_id > FxLimits::MaxBones) {
        FxLogError("Bone ID ({}) is larger than max number of bones!", max_bone_id);
    }

    mesh->VertexList.CreateFrom(positions, normals, uvs, tangents, weights, boneids);

    FxLogInfo("Weights size: {}, ids size: {}", weights.Size, boneids.Size);
    mesh->UploadVertices();
}

// void AxLoaderGltf::LoadSkeleton(FxRef<FxPrimitiveMesh>& mesh, cgltf_skin* skin)
// {
//     if (!skin) {
//         return;
//     }

//     FxSkeleton skel;

//     // Load the bind pose
//     if (skin->inverse_bind_matrices) {
//         cgltf_accessor* accessor = skin->inverse_bind_matrices;
//         skel.InvBindTransforms.InitSize(accessor->count);

//         cgltf_accessor_unpack_floats(accessor, reinterpret_cast<float*>(skel.InvBindTransforms.pData),
//                                      accessor->count * 16);
//     }

//     FxLogInfo("Loaded skin '{}' with {} joints", skin->name ? skin->name : "Unnamed", skin->joints_count);
// }

template <RxImageFormat TFormat>
static void MakeMaterialTextureForPrimitive(FxTSRef<FxMaterial>& material, FxMaterialComponent<TFormat>& component,
                                            cgltf_texture_view& texture_view)
{
    FxAssert(texture_view.texture != nullptr);

    const uint8* image_buffer = cgltf_buffer_view_data(texture_view.texture->image->buffer_view);
    uint32 image_buffer_size = static_cast<uint32>(texture_view.texture->image->buffer_view->size);

    // Stage that shit so we can nuke mGltfData as soon as we can
    uint8* goober_buffer = gEnginePool->Alloc<uint8>(image_buffer_size);
    memcpy(goober_buffer, image_buffer, image_buffer_size);

    // Submit as data to be loaded later by the asset manager
    component.pDataToLoad = FxMakeSlice(const_cast<const uint8*>(goober_buffer), image_buffer_size);
}

void AxLoaderGltf::MakeMaterialForPrimitive(FxTSRef<FxObject>& object, cgltf_primitive* primitive)
{
    cgltf_material* gltf_material = primitive->material;

    if (!gltf_material) {
        return;
    }

    FxTSRef<FxMaterial> material = gMaterialManager->New(object->Name.Get(), &gRenderer->pDeferredRenderer->PlGeometry,
                                                         object->IsSkinned());

    // For some reason the peeber metallic roughness holds our diffuse texture
    if (gltf_material->has_pbr_metallic_roughness) {
        cgltf_texture_view& texture_view = gltf_material->pbr_metallic_roughness.base_color_texture;

        if (!texture_view.texture) {
            // MakeEmptyMaterialTexture(material, material->Diffuse);
            material->Diffuse.pAssetImage = AxImage::GetEmptyImage<RxImageFormat::eRGBA8_UNorm>();

            material->Properties.BaseColor = FxColor::FromFloats(
                gltf_material->pbr_metallic_roughness.base_color_factor);
        }
        else {
            MakeMaterialTextureForPrimitive(material, material->Diffuse, texture_view);
            material->Properties.BaseColor = FxColor::FromRGBA(1, 1, 1, 255);
        }
    }
    else {
        // There is no albedo texture on the model, use the base colour.
        material->Properties.BaseColor = FxColor::FromFloats(gltf_material->pbr_metallic_roughness.base_color_factor);
    }

    // Load the normalmap
    if (gltf_material->normal_texture.texture != nullptr) {
        MakeMaterialTextureForPrimitive(material, material->NormalMap, gltf_material->normal_texture);
    }

    // Load the metallic/roughness texture
    if (gltf_material->pbr_metallic_roughness.metallic_roughness_texture.texture != nullptr) {
        MakeMaterialTextureForPrimitive(material, material->MetallicRoughness,
                                        gltf_material->pbr_metallic_roughness.metallic_roughness_texture);
    }


    object->pMaterial = material;
}

void AxLoaderGltf::UploadMeshToGpu(FxTSRef<FxObject>& object, cgltf_mesh* gltf_mesh, int mesh_index)
{
    const bool has_multiple_primitives = gltf_mesh->primitives_count > 1;

    // Assume at first that there is only one primitive;
    FxTSRef<FxObject> current_object = object;

    // Similarly to `CreateGpuResource`, we are going to make the `object` into a container
    // if there are multiple primitives.
    if (has_multiple_primitives) {
        current_object = FxTSRef<FxObject>::New();
    }

    for (int i = 0; i < gltf_mesh->primitives_count; i++) {
        cgltf_primitive* primitive = &gltf_mesh->primitives[i];

        FxSizedArray<uint32> indices;

        FxRef<FxPrimitiveMesh> primitive_mesh = FxRef<FxPrimitiveMesh>::New();

        // Keep the primitive mesh's vertices and indices in memory if `KeepInMemory` is set
        primitive_mesh->bKeepInMemory = bKeepInMemory;

        // if there are indices in the mesh, add them to the FxPrimitiveMesh
        if (primitive->indices != nullptr) {
            indices.InitSize(primitive->indices->count);

            cgltf_accessor_unpack_indices(primitive->indices, indices.pData, sizeof(uint32), primitive->indices->count);

            // Set the mesh indices
            primitive_mesh->UploadIndices(std::move(indices));
        }

        UnpackMeshAttributes(current_object, primitive_mesh, primitive);
        current_object->pMesh = primitive_mesh;

        MakeMaterialForPrimitive(current_object, primitive);

        primitive_mesh->bIsReady = true;


        if (has_multiple_primitives) {
            // Attach the current object to the object container (our output)
            object->AttachObject(current_object);

            // Create a new object to load into next
            current_object = FxTSRef<FxObject>::New();
        }
    }
}


int32 AxLoaderGltf::FindJointIndex(cgltf_skin* skin, const cgltf_node* node) const
{
    for (cgltf_size i = 0; i < skin->joints_count; i++) {
        if (skin->joints[i] == node) {
            return static_cast<int32>(i);
        }
    }
    return FxBoneNull;
}

void AxLoaderGltf::LoadSkeleton(FxSkeleton& skel, cgltf_skin* skin)
{
    if (!skin) {
        return;
    }

    const uint32 joint_count = static_cast<uint32>(skin->joints_count);
    skel.JointCount = joint_count;

    // Inverse bind matrices
    if (skin->inverse_bind_matrices) {
        cgltf_accessor* accessor = skin->inverse_bind_matrices;
        FxAssert(accessor->count == joint_count);

        skel.InvBindTransforms.InitSize(joint_count);
        cgltf_accessor_unpack_floats(accessor, reinterpret_cast<float32*>(skel.InvBindTransforms.pData),
                                     joint_count * 16);
    }

    // Parent indices and names
    skel.ParentIndices.InitSize(joint_count);
    skel.BoneNames.InitSize(joint_count);
    skel.LocalTransforms.InitSize(joint_count);
    skel.WorldTransforms.InitSize(joint_count);
    skel.SkinningMatrices.InitSize(joint_count);

    for (uint32 i = 0; i < joint_count; i++) {
        const cgltf_node* joint = skin->joints[i];

        skel.BoneNames[i] = joint->name ? FxString(joint->name) : FxString::Fmt("joint_{}", i);
        skel.ParentIndices[i] = joint->parent ? FindJointIndex(skin, joint->parent) : FxBoneNull;
    }
}

void AxLoaderGltf::LoadAnimation(FxAnimation& out_anim, const cgltf_animation& anim, cgltf_skin* skin)
{
    const uint32 joint_count = skin ? static_cast<uint32>(skin->joints_count) : 0;

    out_anim.Name = anim.name ? anim.name : "Unnamed";
    out_anim.Duration = 0.0f;
    out_anim.BoneTracks.InitSize(joint_count);

    for (uint32 i = 0; i < joint_count; i++) {
        out_anim.BoneTracks.pData[i] = FxBoneTrack {};
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

        FxBoneTrack& joint_track = out_anim.BoneTracks.pData[joint_idx];

        const cgltf_size key_count = sampler->input->count;
        const cgltf_size num_components = cgltf_num_components(sampler->output->type);

        FxSizedArray<float32> times;
        times.InitSize(key_count);
        cgltf_accessor_unpack_floats(sampler->input, times.pData, key_count);

        if (key_count > 0) {
            out_anim.Duration = std::max(out_anim.Duration, times.pData[key_count - 1]);
        }

        // Get translations
        if (channel->target_path == cgltf_animation_path_type_translation) {
            FxAssert(sampler->output->type == cgltf_type_vec3);
            joint_track.Translation.Times = std::move(times);
            joint_track.Translation.Values.InitSize(key_count);

            for (uint32 key_index = 0; key_index < key_count; key_index++) {
                float32 buffer[4];

                cgltf_accessor_read_float(sampler->output, key_index, buffer, 3);

                buffer[3] = 0.0f;
                joint_track.Translation.Values[key_index] = (FxVec3f(buffer));
            }
        }

        // Get rotations
        else if (channel->target_path == cgltf_animation_path_type_rotation) {
            FxAssert(sampler->output->type == cgltf_type_vec4);
            joint_track.Rotation.Times = std::move(times);
            joint_track.Rotation.Values.InitSize(key_count);

            for (uint32 key_index = 0; key_index < key_count; key_index++) {
                float32 buffer[4];

                cgltf_accessor_read_float(sampler->output, key_index, buffer, 4);

                joint_track.Rotation.Values[key_index] = FxQuat(buffer);
            }
        }
    }

    FxLogInfo("Loaded animation '{}': {:.3f}s, {} joints", out_anim.Name, out_anim.Duration, joint_count);
}

void AxLoaderGltf::LoadAnimations(FxTSRef<FxObject>& output_object, FxSkeleton& skel)
{
    if (!mpGltfData->animations_count || mpGltfData->skins_count == 0) {
        return;
    }

    // Most models only have one skin/skeleton, so we just assume one for now.
    cgltf_skin* skin = &mpGltfData->skins[0];

    output_object->Animations.InitCapacity(8);


    for (uint32 i = 0; i < mpGltfData->animations_count; i++) {
        FxAnimation anim;
        LoadAnimation(anim, mpGltfData->animations[i], skin);
        output_object->Animations.Insert(std::move(anim));
    }

    FxLogInfo("Loaded {} animations", mpGltfData->animations_count);
}

AxLoaderGltf::Status AxLoaderGltf::LoadFromFile(FxTSRef<AxBase> asset, const std::string& path)
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

AxLoaderGltf::Status AxLoaderGltf::LoadFromMemory(FxTSRef<AxBase> asset, const uint8* data, uint32 size)
{
    cgltf_options options {};

    cgltf_result status = cgltf_parse(&options, data, size, &mpGltfData);
    if (status != cgltf_result_success) {
        FxLogError("Error parsing GLTF file from data");
        return AxLoaderGltf::Status::eError;
    }

    return AxLoaderGltf::Status::eSuccess;
}

void AxLoaderGltf::CreateGpuResource(FxTSRef<AxBase>& asset)
{
    FxTSRef<FxObject> output_object(asset);


    // If there is only one mesh to load, store the mesh directly in the output object
    FxTSRef<FxObject> current_object = output_object;

    // If there are multiple gltf meshes, we will need to use the output object as a
    // container for multiple other meshes
    const bool has_multiple_meshes = mpGltfData->meshes_count > 1;
    if (has_multiple_meshes) {
        current_object = FxTSRef<FxObject>::New();
    }


    FxLogInfo("Unpacking GLTF object with {} meshes", mpGltfData->meshes_count);

    for (int32 node_index = 0; node_index < mpGltfData->nodes_count; node_index++) {
        cgltf_node* node = &mpGltfData->nodes[node_index];

        // Ignore the other GLTF objects. We have alternative internal systems
        if (!node->mesh) {
            continue;
        }

        UploadMeshToGpu(current_object, node->mesh, node_index);

        if (node->skin) {
            // Load a new skeleton
            current_object->pSkeleton = FxRef<FxSkeleton>::New();
            LoadSkeleton(*current_object->pSkeleton, node->skin);

            LoadAnimations(current_object, *current_object->pSkeleton);
        }

        if (has_multiple_meshes) {
            // Attach the current object to the object container (our output)
            output_object->AttachObject(current_object);

            // Create a new object to load into next
            current_object = FxTSRef<FxObject>::New();
        }
    }


    asset = output_object;

    asset->bIsUploadedToGpu = true;
    asset->bIsUploadedToGpu.notify_all();
}

void AxLoaderGltf::Destroy(FxTSRef<AxBase>& asset)
{
    if (mpGltfData) {
        cgltf_free(mpGltfData);
        mpGltfData = nullptr;
    }
}
