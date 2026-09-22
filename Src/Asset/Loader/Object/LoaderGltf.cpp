#include "LoaderGltf.hpp"

#include "../Image/LoaderKtx.hpp"
#include "../Image/LoaderStb.hpp"

#include <ThirdParty/cgltf.h>

#include <Asset/Animation.hpp>
#include <Asset/AssetBase.hpp>
#include <Asset/AssetManager.hpp>
#include <Core/Ref.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/Object.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/PrimitiveMesh.hpp>

// Renderer includes
#include <Renderer/Backend/GraphicsBackendFwd.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/MeshUtil.hpp>
#include <cmath>

namespace fx {

using namespace renderer;

namespace loader {

void LoaderGltf::UnpackMeshAttributes(Object* object, Ref<PrimitiveMesh>& mesh, cgltf_primitive* primitive,
									  bool is_skinned)
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
		else if (is_skinned && attribute->type == cgltf_attribute_type_weights) {
			cgltf_size data_size = cgltf_accessor_unpack_floats(attribute->data, nullptr, 0);
			weights.InitSize(data_size);
			cgltf_accessor_unpack_floats(attribute->data, weights.pData, data_size);
		}
		else if (is_skinned && attribute->type == cgltf_attribute_type_joints) {
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

static constexpr uint8 scKtx2Magic[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };

static bool HasKtx2Magic(const uint8* data, size_t size)
{
	return data != nullptr && size >= sizeof(scKtx2Magic) && memcmp(data, scKtx2Magic, sizeof(scKtx2Magic)) == 0;
}

/*
 * @brief KHR_texture_basisu points at the KTX2 image through the extension (leaving `source` as a png or jpeg fallback
 * for other loaders) but some exporters just put the KTX2 image straight into `source`. Check for both
 */
static const cgltf_image* GetTextureImage(const cgltf_texture* texture)
{
	if (texture->has_basisu && texture->basisu_image != nullptr) {
		return texture->basisu_image;
	}

	return texture->image;
}

static const char* GetImageName(const cgltf_image* image)
{
	if (image->name != nullptr) {
		return image->name;
	}

	if (image->uri != nullptr && strncmp(image->uri, "data:", 5) != 0) {
		return image->uri;
	}

	return "<unnamed>";
}

/**
 * @brief Loads the KTX2 image that a GLTF image refers to. It is either embedded in a buffer view, inlined as a base64
 * `data:` entry or stored as an external file relative to the model
 */
static bool OpenGltfKtxImage(loader::LoaderKtx& ktx, const cgltf_image* image, const String& model_path)
{
	if (image->buffer_view != nullptr) {
		const uint8* data = cgltf_buffer_view_data(image->buffer_view);
		const size_t size = image->buffer_view->size;

		return HasKtx2Magic(data, size) && ktx.OpenFromMemory(data, static_cast<uint32>(size));
	}

	if (image->uri == nullptr) {
		return false;
	}

	// Inline base64 encoded gubbins
	if (strncmp(image->uri, "data:", 5) == 0) {
		const char* comma = strchr(image->uri, ',');

		if (comma == nullptr || comma - image->uri < 7 || strncmp(comma - 7, ";base64", 7) != 0) {
			return false;
		}

		const char* base64 = comma + 1;
		const size_t base64_length = strlen(base64);

		size_t padding = 0;
		while (padding < base64_length && padding < 2 && base64[base64_length - 1 - padding] == '=') {
			padding++;
		}

		const size_t size = base64_length / 4 * 3 - padding;

		cgltf_options options {};
		void* data = nullptr;

		if (size == 0 || cgltf_load_buffer_base64(&options, size, base64, &data) != cgltf_result_success) {
			return false;
		}

		const bool opened = HasKtx2Magic(static_cast<const uint8*>(data), size) &&
							ktx.OpenFromMemory(static_cast<const uint8*>(data), static_cast<uint32>(size));
		std::free(data);

		return opened;
	}

	// External file, relative to the directory containing the model
	std::string uri = image->uri;
	uri.resize(cgltf_decode_uri(uri.data()));

	const int32 slash_index = model_path.FindLast('/');
	const std::string full_path = (slash_index >= 0) ? std::string(model_path.CStr(), slash_index + 1) + uri : uri;

	// Check the header first so a PNG/JPEG gets an earlier error rather than one from the KTX loader
	uint8 header[sizeof(scKtx2Magic)] {};
	std::FILE* fp = std::fopen(full_path.c_str(), "rb");

	if (fp == nullptr) {
		LogError(LC_ASSET, "Could not open glTF texture '{}'", full_path.c_str());
		return false;
	}

	const size_t header_size = std::fread(header, 1, sizeof(header), fp);
	std::fclose(fp);

	return HasKtx2Magic(header, header_size) && ktx.Open(full_path.c_str());
}

static bool IsRgba8Format(eImageFormat format)
{
	return format == eImageFormat::RGBA8_UNorm || format == eImageFormat::RGBA8_SRGB;
}

/// Whether any texel in the base level of an 8-bit, 4 channel image is not fully opaque.
static bool ImageHasTranslucentTexels(const ImageInfo& info)
{
	if (!IsRgba8Format(info.Format) && info.Format != eImageFormat::BGRA8_UNorm) {
		return false;
	}

	// The base level comes first in the packed mip chain
	const uint64 texel_count = std::min(uint64(info.Size.X) * info.Size.Y, info.ImageData.Size / 4);

	for (uint64 i = 0; i < texel_count; i++) {
		if (info.ImageData.pData[i * 4 + 3] != 0xFF) {
			return true;
		}
	}

	return false;
}

/**
 * @brief Loads a material texture's mip chain from its KTX2 image. Material textures must be KTX2, and anything else
 * logs an error and returns false.
 */
static bool MakeMaterialTextureForPrimitive(const String& model_path, Material* material, const char* component_name,
											MaterialComponent& component, const cgltf_texture_view& texture_view,
											bool* out_has_alpha = nullptr)
{
	Assert(texture_view.texture != nullptr);

	const cgltf_image* image = GetTextureImage(texture_view.texture);

	if (image == nullptr) {
		return false;
	}

	loader::LoaderKtx ktx {};

	if (!OpenGltfKtxImage(ktx, image, model_path) || !ktx.IsOpen()) {
		LogError(LC_ASSET, "glTF textures must be KTX2 (image '{}', {} of material '{}' in '{}')", GetImageName(image),
				 component_name, material->Name.Get(), model_path);
		return false;
	}

	ImageInfo image_info = ktx.MakeImageInfo();

	if (image_info.ImageData.pData == nullptr) {
		return false;
	}

	// The component decides the colour space (albedo is sRGB, normal and surface maps are linear), not whatever the
	// exporter tagged the file with. Both are the same bits.
	if (IsRgba8Format(image_info.Format) && IsRgba8Format(component.ImageFormat) &&
		image_info.Format != component.ImageFormat) {
		LogWarning(LC_ASSET, "glTF texture '{}' ({}) is stored as {}, expected {}", GetImageName(image), component_name,
				   (image_info.Format == eImageFormat::RGBA8_SRGB) ? "sRGB" : "UNorm",
				   (component.ImageFormat == eImageFormat::RGBA8_SRGB) ? "sRGB" : "UNorm");
		image_info.Format = component.ImageFormat;
	}

	if (out_has_alpha != nullptr) {
		*out_has_alpha = ImageHasTranslucentTexels(image_info);
	}

	component.UploadSrc = eMaterialComponentUploadSrc::DirectUpload;
	component.ImageToUpload = image_info;

	return true;
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


	MaterialID material_id = gMaterialManager->NewMaterial(material_name, ePipelineName::Geometry, object->IsSkinned());
	Material* material = gMaterialManager->GetMaterial(material_id);

	object->SetMaterial(material_id);

	// KHR_materials_pbrSpecularGlossiness takes priority when present; any pbrMetallicRoughness block alongside it is
	// only a fallback for loaders that don't support the extension.
	const bool use_specular_glossiness = gltf_material->has_pbr_specular_glossiness;

	cgltf_pbr_metallic_roughness& gltf_mr = gltf_material->pbr_metallic_roughness;
	cgltf_pbr_specular_glossiness& gltf_sg = gltf_material->pbr_specular_glossiness;

	// Diffuse lives in whichever PBR block the material uses
	cgltf_texture_view* diffuse_view = nullptr;

	if (use_specular_glossiness) {
		diffuse_view = &gltf_sg.diffuse_texture;
	}
	else if (gltf_material->has_pbr_metallic_roughness) {
		diffuse_view = &gltf_mr.base_color_texture;
	}

	// Only blended materials care whether the texture has alpha, and finding out means scanning every texel
	bool diffuse_has_alpha = false;
	bool* out_diffuse_has_alpha = (gltf_material->alpha_mode == cgltf_alpha_mode_blend) ? &diffuse_has_alpha : nullptr;

	const bool has_diffuse = (diffuse_view != nullptr && diffuse_view->texture != nullptr) &&
							 MakeMaterialTextureForPrimitive(mModelPath, material, "diffuse", material->Diffuse,
															 *diffuse_view, out_diffuse_has_alpha);

	if (!has_diffuse) {
		material->Diffuse.SetTicket(gAssetManager->GetNullImageTicket(eImageFormat::RGBA8_UNorm));
	}

	if (use_specular_glossiness) {
		material->SetBaseColorFactor(gltf_sg.diffuse_factor);
	}
	else if (gltf_material->has_pbr_metallic_roughness) {
		material->SetBaseColorFactor(gltf_mr.base_color_factor);
	}

	// Load the normalmap
	if (gltf_material->normal_texture.texture != nullptr) {
		MakeMaterialTextureForPrimitive(mModelPath, material, "normal", material->NormalMap,
										gltf_material->normal_texture);
	}

	// Load the surface texture and factors. cgltf fills in the glTF defaults for any factor the file leaves out
	if (use_specular_glossiness) {
		material->SetSpecularGlossiness(gltf_sg.specular_factor, gltf_sg.glossiness_factor);

		if (gltf_sg.specular_glossiness_texture.texture != nullptr) {
			MakeMaterialTextureForPrimitive(mModelPath, material, "specular-glossiness", material->MetallicRoughness,
											gltf_sg.specular_glossiness_texture);
		}
	}
	else {
		material->SetMetallicRoughness(gltf_mr.metallic_factor, gltf_mr.roughness_factor);

		if (gltf_mr.metallic_roughness_texture.texture != nullptr) {
			MakeMaterialTextureForPrimitive(mModelPath, material, "metallic-roughness", material->MetallicRoughness,
											gltf_mr.metallic_roughness_texture);
		}
	}

	// Handle glTF alpha mode / baseColorFactor alpha
	{
		float baseAlpha = 1.0f;
		if (use_specular_glossiness) {
			baseAlpha = gltf_sg.diffuse_factor[3];
		}
		else if (gltf_material->has_pbr_metallic_roughness) {
			baseAlpha = gltf_mr.base_color_factor[3];
		}

		if (gltf_material->alpha_mode == cgltf_alpha_mode_blend) {
			// Blend mode: use baseColor alpha (and texture alpha in shader) for transparency
			if (baseAlpha < 0.99f) {
				material->SetAlpha(baseAlpha);
			}
			else if (diffuse_has_alpha) {
				// If baseColor is opaque but texture may have alpha, force transparent path
				// so texAlpha * matAlpha blending works with depthWrite=false.
				material->SetAlpha(0.99f);
			}
			else {
				// Nothing to blend; exporters often mark opaque materials as BLEND. Drawing those through the
				// unsorted, depth-write-less transparent path makes a model's own faces overdraw each other.
				material->SetAlpha(1.0f);
			}
		}
		else if (gltf_material->alpha_mode == cgltf_alpha_mode_mask) {
			// Mask mode: opaque pipeline with alpha-test at 0.5 (shader ALPHA_CUTOFF)
			material->SetAlphaMask(true);
			material->SetAlpha(baseAlpha);
			// Keep on opaque path; shader discard will handle cutout
		}
		else { // opaque
			if (baseAlpha < 0.99f) {
				// Opaque material with translucent base factor -> treat as blended
				material->SetAlpha(baseAlpha);
			}
			else {
				material->SetAlpha(1.0f);
			}
		}

		// Cutouts and blended surfaces (leaves, fences, glass) are the ones that have to show both faces. Exporters set
		// doubleSided on nearly every material regardless, and drawing closed opaque meshes without culling would only
		// cost fill rate, so it is only honored for these.
		if (gltf_material->double_sided && gltf_material->alpha_mode != cgltf_alpha_mode_opaque) {
			material->SetDoubleSided(true);
		}

		if (gltf_material->unlit) {
			material->SetUnlit(true);
		}
	}

	material->Finalize();
}

void LoaderGltf::BuildObjectsFromPrimitives(Object* container_object, cgltf_mesh* gltf_mesh, bool is_skinned)
{
	const bool has_multiple_primitives = gltf_mesh->primitives_count > 1;

	// Assume at first that there is only one primitive;
	Object* current_object = container_object;

	// Similarly to `CreateGpuResource`, we are going to make the `object` into a container
	// if there are multiple primitives.
	if (has_multiple_primitives) {
		current_object = gObjectManager->NewObject(container_object->Name.Get(), MaterialID::scNull);
	}

	bool needs_new_object = false;

	for (int i = 0; i < gltf_mesh->primitives_count; i++) {
		if (needs_new_object) {
			// Create a new object to load into next
			current_object = gObjectManager->NewObject(container_object->Name.Get(), MaterialID::scNull);
			needs_new_object = false;
		}


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

		UnpackMeshAttributes(current_object, primitive_mesh, gltf_primitive, is_skinned);
		current_object->pMesh = primitive_mesh;
		current_object->Bounds = MeshUtil::CalculateBounds(primitive_mesh->GetVertices());

		MakeMaterialForPrimitive(current_object, gltf_primitive, i);

		if (has_multiple_primitives) {
			// Attach the current object to the object container (our output)
			container_object->AttachObject(current_object->ID);
			needs_new_object = true;
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
		primitive_mesh->UploadIndices(GraphicsBackendFwd::GetUploadCmd());
		primitive_mesh->UploadVertices(GraphicsBackendFwd::GetUploadCmd());

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


/// Pulls the joint node's own local transform out of the GLTF, mirrored into engine space the same way
/// the animation channels are (negate X on translations, negate Y/Z on rotations).
///
/// Every component an animation does not drive has to fall back to this, otherwise the joint lands at
/// identity: bones collapse onto their parent's origin and the skin smears across the model.
static BoneRestPose MakeRestPoseForJoint(const cgltf_node* joint)
{
	BoneRestPose rest {};

	if (joint->has_matrix) {
		// Decompose the raw matrix. GLTF stores matrices column-major, which lines up directly with
		// the engine's row-major/row-vector matrices, so no transpose is needed here.
		const Mat4f m(joint->matrix);

		Vec3f axis_x = Vec3f(m.Rows[0]);
		Vec3f axis_y = Vec3f(m.Rows[1]);
		Vec3f axis_z = Vec3f(m.Rows[2]);

		const float32 scale_x = axis_x.Length();
		const float32 scale_y = axis_y.Length();
		const float32 scale_z = axis_z.Length();

		rest.Scale = Vec3f(scale_x, scale_y, scale_z);
		rest.Translation = m.GetTranslation();

		if (scale_x > 1e-8f && scale_y > 1e-8f && scale_z > 1e-8f) {
			axis_x = axis_x / scale_x;
			axis_y = axis_y / scale_y;
			axis_z = axis_z / scale_z;

			// Row-vector rotation basis -> quaternion.
			const float32 trace = axis_x.X + axis_y.Y + axis_z.Z;

			if (trace > 0.0f) {
				const float32 w = std::sqrt(trace + 1.0f) * 0.5f;
				const float32 inv = 0.25f / w;

				rest.Rotation = Quat((axis_y.Z - axis_z.Y) * inv, (axis_z.X - axis_x.Z) * inv,
									 (axis_x.Y - axis_y.X) * inv, w);
			}
			else if (axis_x.X > axis_y.Y && axis_x.X > axis_z.Z) {
				const float32 x = std::sqrt((1.0f + axis_x.X - axis_y.Y - axis_z.Z)) * 0.5f;
				const float32 inv = 0.25f / x;

				rest.Rotation = Quat(x, (axis_x.Y + axis_y.X) * inv, (axis_z.X + axis_x.Z) * inv,
									 (axis_y.Z - axis_z.Y) * inv);
			}
			else if (axis_y.Y > axis_z.Z) {
				const float32 y = std::sqrt((1.0f + axis_y.Y - axis_x.X - axis_z.Z)) * 0.5f;
				const float32 inv = 0.25f / y;

				rest.Rotation = Quat((axis_x.Y + axis_y.X) * inv, y, (axis_y.Z + axis_z.Y) * inv,
									 (axis_z.X - axis_x.Z) * inv);
			}
			else {
				const float32 z = std::sqrt((1.0f + axis_z.Z - axis_x.X - axis_y.Y)) * 0.5f;
				const float32 inv = 0.25f / z;

				rest.Rotation = Quat((axis_z.X + axis_x.Z) * inv, (axis_y.Z + axis_z.Y) * inv, z,
									 (axis_x.Y - axis_y.X) * inv);
			}
		}
	}
	else {
		if (joint->has_translation) {
			rest.Translation = Vec3f(joint->translation[0], joint->translation[1], joint->translation[2]);
		}

		if (joint->has_rotation) {
			rest.Rotation = Quat(joint->rotation[0], joint->rotation[1], joint->rotation[2], joint->rotation[3]);
		}

		if (joint->has_scale) {
			rest.Scale = Vec3f(joint->scale[0], joint->scale[1], joint->scale[2]);
		}
	}

	// Mirror across X to match the vertex buffers and the animation tracks.
	rest.Translation = Vec3f::FlipSigns<-1, 1, 1, 1>(rest.Translation);
	rest.Rotation = Quat(rest.Rotation.GetX(), -rest.Rotation.GetY(), -rest.Rotation.GetZ(), rest.Rotation.GetW());

	return rest;
}

void LoaderGltf::LoadSkeleton(Skeleton& skel, cgltf_skin* skin)
{
	if (!skin) {
		return;
	}

	const uint32 joint_count = static_cast<uint32>(skin->joints_count);
	skel.JointCount = joint_count;

	// The GLTF coordinate system is garbage, reflect so -X becomes X (this also changes rotation back from CCW to
	// CW). Everything loaded out of the skin has to be conjugated by this to stay in step with the vertex buffers.
	Mat4f reflection = Mat4f::scIdentity;
	reflection.Rows[0].X = -1.0f;

	// Inverse bind matrices
	if (skin->inverse_bind_matrices) {
		cgltf_accessor* accessor = skin->inverse_bind_matrices;
		Assert(accessor->count == joint_count);

		skel.InvBindTransforms.InitSize(joint_count);
		cgltf_accessor_unpack_floats(accessor, reinterpret_cast<float32*>(skel.InvBindTransforms.pData),
									 joint_count * 16);

		for (uint32 i = 0; i < joint_count; i++) {
			Mat4f& m = skel.InvBindTransforms[i];

			m = reflection * m * reflection;
		}
	}

	// Parent indices and names
	skel.ParentIndices.InitSize(joint_count);
	skel.BoneNames.InitSize(joint_count);
	skel.RestPose.InitSize(joint_count);
	skel.RootTransforms.InitSize(joint_count);
	skel.LocalTransforms.InitSize(joint_count);
	skel.WorldTransforms.InitSize(joint_count);
	skel.SkinningMatrices.InitSize(joint_count);

	for (uint32 i = 0; i < joint_count; i++) {
		const cgltf_node* joint = skin->joints[i];

		skel.BoneNames[i] = joint->name ? String(joint->name) : String::Fmt("joint_{}", i);
		skel.ParentIndices[i] = joint->parent ? FindJointIndex(skin, joint->parent) : BoneNull;
		skel.RestPose[i] = MakeRestPoseForJoint(joint);

		const bool is_root_joint = (skel.ParentIndices[i] == BoneNull);

		skel.RootTransforms[i] = Mat4f::scIdentity;

		if (is_root_joint && joint->parent) {
			Mat4f node_world;
			cgltf_node_transform_world(joint->parent, reinterpret_cast<float32*>(&node_world));

			skel.RootTransforms[i] = reflection * node_world * reflection;
		}
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

		// Get scales
		else if (channel->target_path == cgltf_animation_path_type_scale) {
			Assert(sampler->output->type == cgltf_type_vec3);
			joint_track.Scale.Times = std::move(times);
			joint_track.Scale.Values.InitSize(key_count);

			for (uint32 key_index = 0; key_index < key_count; key_index++) {
				float32 buffer[4];

				cgltf_accessor_read_float(sampler->output, key_index, buffer, 3);
				buffer[3] = 0.0f;

				joint_track.Scale.Values[key_index] = (Vec3f(buffer));
			}
		}
	}

	LogInfo(LC_ASSET, "Loaded animation '{}': {:.3f}s, {} joints", out_anim.Name, out_anim.Duration, joint_count);
}

void LoaderGltf::LoadAnimations(Skeleton& skel, cgltf_skin* skin)
{
	if (!mpGltfData->animations_count || !skin) {
		return;
	}

	skel.Animations.InitCapacity(mpGltfData->animations_count);

	for (uint32 i = 0; i < mpGltfData->animations_count; i++) {
		Animation anim;
		LoadAnimation(anim, mpGltfData->animations[i], skin);
		skel.Animations.Insert(std::move(anim));
	}

	// Loop the first animation by default. The owner can swap it out with SetRestAnimation(), or pass null to hold the
	// rest pose.
	skel.SetRestAnimation(&skel.Animations[0]);

	LogInfo(LC_ASSET, "Loaded {} animations", mpGltfData->animations_count);
}

void LoaderGltf::ProcessData(AssetTicket& ticket)
{
	// If there is only one mesh to load, store the mesh directly in the output object
	Object* output_object = static_cast<Object*>(ticket.Get());

	// If there are multiple gltf meshes, we will need to use the output object as a
	// container for multiple other meshes
	const bool has_multiple_meshes = mpGltfData->meshes_count > 1;

	int32 attach_id = 1;

	// Skeletons (and their animations) built so far, indexed by skin. Meshes skinned to the same skin share one
	// skeleton rather than each parsing their own copy.
	std::vector<Ref<Skeleton>> skeletons(mpGltfData->skins_count);

	// Load each mesh node in the GLTF as a new separate object.
	for (int32 node_index = 0; node_index < mpGltfData->nodes_count; node_index++) {
		cgltf_node* node = &mpGltfData->nodes[node_index];

		// Only nodes that reference a mesh become objects. Skinned models can have hundreds of joint nodes (and other
		// transform-only nodes); giving each of those an object quickly exhausts the object pool.
		if (!node->mesh) {
			continue;
		}

		Object* current_object = output_object;

		// If there are multiple meshes, each one gets its own object that is attached to the output object.
		if (has_multiple_meshes) {
			current_object = gObjectManager->NewObject(std::format("{}_{}", output_object->Name.Get(), attach_id++),
													   MaterialID::scNull);
		}

		// Load all meshes from the node we are currently on.
		BuildObjectsFromPrimitives(current_object, node->mesh, node->skin != nullptr);

		// If the node has a skeleton, load it in.
		if (node->skin) {
			Ref<Skeleton>& skeleton = skeletons[node->skin - mpGltfData->skins];

			if (!skeleton) {
				skeleton = Ref<Skeleton>::New();
				LoadSkeleton(*skeleton, node->skin);
				LoadAnimations(*skeleton, node->skin);
			}

			current_object->pSkeleton = skeleton;

			// A mesh with multiple primitives is split into attached objects, which each draw skinned vertices and
			// need the skeleton themselves.
			for (ObjectID primitive_id : current_object->AttachedNodes) {
				gObjectManager->GetObject(primitive_id)->pSkeleton = skeleton;
			}
		}

		if (has_multiple_meshes) {
			// Attach the loaded object onto the final object. This will be a container.
			output_object->AttachObject(current_object->ID);
		}
	}
}

eLoaderStatus LoaderGltf::Load(AssetTicket& ticket, const String& path)
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

	ProcessData(ticket);

	return eLoaderStatus::Success;
}


eLoaderStatus LoaderGltf::Load(AssetTicket& ticket, const uint8* data, uint32 size)
{
	cgltf_options options {};

	cgltf_result status = cgltf_parse(&options, data, size, &mpGltfData);
	if (status != cgltf_result_success) {
		LogError(LC_ASSET, "Error parsing GLTF file from data");
		return eLoaderStatus::Error;
	}

	ProcessData(ticket);

	return eLoaderStatus::Success;
}

void LoaderGltf::CreateGpuResource(AssetTicket& ticket)
{
	// If there is only one mesh to load, store the mesh directly in the output object
	// TSRef<Object> current_object = container_object;

	// If there are multiple gltf meshes, we will need to use the output object as a
	// container for multiple other meshes
	// const bool has_multiple_meshes = mpGltfData->meshes_count > 1;

	// UploadMeshToGpu(container_object);

	Object* object = static_cast<Object*>(ticket.Get());

	uint32 attached_count = object->AttachedNodes.Size();

	// container_object->PrintDebug();

	for (uint32 i = 0; i < attached_count; i++) {
		UploadMeshToGpu(gObjectManager->GetObject(object->AttachedNodes[i]));
	}

	UploadMeshToGpu(object);
	ticket.SignalUploadedToGpu();
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
