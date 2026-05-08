#include "AxLoaderGltf.hpp"

#include <ThirdParty/cgltf.h>

#include <Asset/Animation.hpp>
#include <Asset/AxBase.hpp>
#include <Asset/AxManager.hpp>
#include <Core/Ref.hpp>
#include <Material.hpp>
#include <Object.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/PrimitiveMesh.hpp>

// Renderer includes
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx {

using namespace renderer;

TSRef<AxImage> LoadTexture(const TSRef<Material>& material, const cgltf_texture_view& texture_view);

void AxLoaderGltf::UnpackMeshAttributes(const TSRef<Object>& object, Ref<PrimitiveMesh>& mesh,
                                        cgltf_primitive* primitive)
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

    mesh->UploadVertices();
}

// void AxLoaderGltf::LoadSkeleton(Ref<PrimitiveMesh>& mesh, cgltf_skin* skin)
// {
//     if (!skin) {
//         return;
//     }

//     Skeleton skel;

//     // Load the bind pose
//     if (skin->inverse_bind_matrices) {
//         cgltf_accessor* accessor = skin->inverse_bind_matrices;
//         skel.InvBindTransforms.InitSize(accessor->count);

//         cgltf_accessor_unpack_floats(accessor, reinterpret_cast<float*>(skel.InvBindTransforms.pData),
//                                      accessor->count * 16);
//     }

//     LogInfo("Loaded skin '{}' with {} joints", skin->name ? skin->name : "Unnamed", skin->joints_count);
// }

template <eImageFormat TFormat>
static void MakeMaterialTextureForPrimitive(TSRef<Material>& material, MaterialComponent<TFormat>& component,
                                            cgltf_texture_view& texture_view)
{
    Assert(texture_view.texture != nullptr);

    const uint8* image_buffer = cgltf_buffer_view_data(texture_view.texture->image->buffer_view);
    uint32 image_buffer_size = static_cast<uint32>(texture_view.texture->image->buffer_view->size);

    // Stage that shit so we can nuke mGltfData as soon as we can
    uint8* goober_buffer = static_cast<uint8*>(std::malloc(image_buffer_size));
    memcpy(goober_buffer, image_buffer, image_buffer_size);

    // Submit as data to be loaded later by the asset manager
    component.pDataToLoad = MakeSlice(const_cast<const uint8*>(goober_buffer), image_buffer_size);
}

void AxLoaderGltf::MakeMaterialForPrimitive(TSRef<Object>& object, cgltf_primitive* primitive)
{
    cgltf_material* gltf_material = primitive->material;

    if (!gltf_material) {
        return;
    }

    TSRef<Material> material = gMaterialManager->New(
        object->Name.Get(), &gPipelineCache->Request(ePipelineName::Geometry), object->IsSkinned());

    // For some reason the peeber metallic roughness holds our diffuse texture
    if (gltf_material->has_pbr_metallic_roughness) {
        cgltf_texture_view& texture_view = gltf_material->pbr_metallic_roughness.base_color_texture;

        if (!texture_view.texture) {
            // MakeEmptyMaterialTexture(material, material->Diffuse);
            material->Diffuse.pAssetImage = AxImage::GetEmptyImage<eImageFormat::RGBA8_UNorm>();

            material->Properties.BaseColor = Color::FromFloats(gltf_material->pbr_metallic_roughness.base_color_factor);
        }
        else {
            MakeMaterialTextureForPrimitive(material, material->Diffuse, texture_view);
            material->Properties.BaseColor = Color::FromRGBA(1, 1, 1, 255);
        }
    }
    else {
        // There is no albedo texture on the model, use the base colour.
        material->Properties.BaseColor = Color::FromFloats(gltf_material->pbr_metallic_roughness.base_color_factor);
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

void AxLoaderGltf::UploadMeshToGpu(TSRef<Object>& object, cgltf_mesh* gltf_mesh, int mesh_index)
{
    const bool has_multiple_primitives = gltf_mesh->primitives_count > 1;

    // Assume at first that there is only one primitive;
    TSRef<Object> current_object = object;

    // Similarly to `CreateGpuResource`, we are going to make the `object` into a container
    // if there are multiple primitives.
    if (has_multiple_primitives) {
        current_object = TSRef<Object>::New();
    }

    for (int i = 0; i < gltf_mesh->primitives_count; i++) {
        cgltf_primitive* primitive = &gltf_mesh->primitives[i];

        SizedArray<uint32> indices;

        Ref<PrimitiveMesh> primitive_mesh = Ref<PrimitiveMesh>::New();

        // Keep the primitive mesh's vertices and indices in memory if `KeepInMemory` is set
        primitive_mesh->bKeepInMemory = bKeepInMemory;

        // If there are indices in the mesh, add them to the PrimitiveMesh
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
            current_object = TSRef<Object>::New();
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

    return BoneNull;
}

void ConvertFromGltfMatrix(Mat4f& m)
{
    // m.Columns[1] = Vec4f::FlipSigns<1, 1, -1, 1>(m.Columns[1]);
    // m.Columns[2] = Vec4f::FlipSigns<1, -1, 1, 1>(m.Columns[2]);
    // m.Columns[3] = Vec4f::FlipSigns<-1, 1, 1, 1>(m.Columns[3]);
}


void AxLoaderGltf::LoadSkeleton(Skeleton& skel, cgltf_skin* skin)
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

            ConvertFromGltfMatrix(m);
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

void AxLoaderGltf::LoadAnimation(Animation& out_anim, const cgltf_animation& anim, cgltf_skin* skin)
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

    LogInfo("Loaded animation '{}': {:.3f}s, {} joints", out_anim.Name, out_anim.Duration, joint_count);
}

void AxLoaderGltf::LoadAnimations(TSRef<Object>& output_object, Skeleton& skel)
{
    if (!mpGltfData->animations_count || mpGltfData->skins_count == 0) {
        return;
    }

    // Most models only have one skin/skeleton, so we just assume one for now.
    cgltf_skin* skin = &mpGltfData->skins[0];

    output_object->Animations.InitCapacity(8);


    for (uint32 i = 0; i < mpGltfData->animations_count; i++) {
        Animation anim;
        LoadAnimation(anim, mpGltfData->animations[i], skin);
        output_object->Animations.Insert(std::move(anim));
    }

    LogInfo("Loaded {} animations", mpGltfData->animations_count);
}

AxLoaderGltf::Status AxLoaderGltf::LoadFromFile(TSRef<AxBase> asset, const String& path)
{
    cgltf_options options {};

    cgltf_result status = cgltf_parse_file(&options, path.CStr(), &mpGltfData);
    if (status != cgltf_result_success) {
        LogError("Error parsing GLTF file! (path: {})", path);
        return AxLoaderGltf::Status::Error;
    }

    status = cgltf_load_buffers(&options, mpGltfData, path.CStr());
    if (status != cgltf_result_success) {
        LogError("Error loading buffers from GLTF file! (path: {:s})", path);

        return AxLoaderGltf::Status::Error;
    }


    return AxLoaderGltf::Status::Success;
}

AxLoaderGltf::Status AxLoaderGltf::LoadFromMemory(TSRef<AxBase> asset, const uint8* data, uint32 size)
{
    cgltf_options options {};

    cgltf_result status = cgltf_parse(&options, data, size, &mpGltfData);
    if (status != cgltf_result_success) {
        LogError("Error parsing GLTF file from data");
        return AxLoaderGltf::Status::Error;
    }

    return AxLoaderGltf::Status::Success;
}

void AxLoaderGltf::CreateGpuResource(TSRef<AxBase>& asset)
{
    TSRef<Object> output_object(asset);


    // If there is only one mesh to load, store the mesh directly in the output object
    TSRef<Object> current_object = output_object;

    // If there are multiple gltf meshes, we will need to use the output object as a
    // container for multiple other meshes
    const bool has_multiple_meshes = mpGltfData->meshes_count > 1;
    if (has_multiple_meshes) {
        current_object = TSRef<Object>::New();
    }


    LogInfo("Unpacking GLTF object with {} meshes", mpGltfData->meshes_count);

    for (int32 node_index = 0; node_index < mpGltfData->nodes_count; node_index++) {
        cgltf_node* node = &mpGltfData->nodes[node_index];

        // Ignore the other GLTF objects. We have alternative internal systems
        if (!node->mesh) {
            continue;
        }

        UploadMeshToGpu(current_object, node->mesh, node_index);

        if (node->skin) {
            // Load a new skeleton
            current_object->pSkeleton = Ref<Skeleton>::New();
            LoadSkeleton(*current_object->pSkeleton, node->skin);

            LoadAnimations(current_object, *current_object->pSkeleton);
        }

        if (has_multiple_meshes) {
            // Attach the current object to the object container (our output)
            output_object->AttachObject(current_object);

            // Create a new object to load into next
            current_object = TSRef<Object>::New();
        }
    }


    asset = output_object;

    asset->bIsUploadedToGpu = true;
    asset->bIsUploadedToGpu.notify_all();
}

void AxLoaderGltf::Destroy(TSRef<AxBase>& asset)
{
    if (mpGltfData) {
        cgltf_free(mpGltfData);
        mpGltfData = nullptr;
    }
}

} // namespace fx
