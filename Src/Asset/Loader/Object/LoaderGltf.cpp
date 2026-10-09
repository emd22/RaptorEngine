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
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fx {

using namespace renderer;

namespace loader {

namespace {

constexpr uint32 scMaxUvSets = 4;
constexpr uint32 scInfluencesPerVertex = 4;

struct MeshAttributes
{
	SizedArray<float32> Positions;
	SizedArray<float32> Normals;
	SizedArray<float32> Uvs;
	SizedArray<float32> Tangents;
	SizedArray<float32> Weights;
	SizedArray<uint32> BoneIds;
};

struct UvChoice
{
	uint32 Set = 0;
	bool bHasTransform = false;
	cgltf_texture_transform Transform {};
	bool bMismatch = false;
};

bool UnpackFloats(const cgltf_accessor* accessor, cgltf_type expected_type, SizedArray<float32>& out)
{
	if (accessor == nullptr || accessor->type != expected_type) {
		return false;
	}

	const cgltf_size float_count = cgltf_accessor_unpack_floats(accessor, nullptr, 0);

	if (float_count == 0) {
		return false;
	}

	SizedArray<float32> unpacked;
	unpacked.InitSize(float_count);

	if (cgltf_accessor_unpack_floats(accessor, unpacked.pData, float_count) != float_count) {
		return false;
	}

	out = std::move(unpacked);

	return true;
}

bool UnpackJoints(const cgltf_accessor* accessor, SizedArray<uint32>& out)
{
	if (accessor == nullptr || accessor->type != cgltf_type_vec4 || accessor->count == 0) {
		return false;
	}

	SizedArray<uint32> unpacked;
	unpacked.InitSize(accessor->count * scInfluencesPerVertex);

	for (cgltf_size i = 0; i < accessor->count; i++) {
		if (!cgltf_accessor_read_uint(accessor, i,
									  reinterpret_cast<cgltf_uint*>(&unpacked.pData[i * scInfluencesPerVertex]),
									  scInfluencesPerVertex)) {
			return false;
		}
	}

	out = std::move(unpacked);

	return true;
}

bool IsPrimitiveSupported(const cgltf_primitive* primitive)
{
	return primitive->type == cgltf_primitive_type_triangles ||
		   primitive->type == cgltf_primitive_type_triangle_strip ||
		   primitive->type == cgltf_primitive_type_triangle_fan;
}

bool BuildTriangleIndices(const cgltf_primitive* primitive, uint32 vertex_count, SizedArray<uint32>& out_indices)
{
	SizedArray<uint32> source;

	if (primitive->indices != nullptr) {
		const cgltf_size count = primitive->indices->count;

		if (count == 0) {
			return false;
		}

		source.InitSize(count);

		if (cgltf_accessor_unpack_indices(primitive->indices, source.pData, sizeof(uint32), count) != count) {
			return false;
		}
	}
	else {
		source.InitSize(vertex_count);

		for (uint32 i = 0; i < vertex_count; i++) {
			source[i] = i;
		}
	}

	for (uint32 i = 0; i < source.Size; i++) {
		if (source[i] >= vertex_count) {
			return false;
		}
	}

	if (primitive->type == cgltf_primitive_type_triangles) {
		const uint32 usable = static_cast<uint32>((source.Size / 3) * 3);

		if (usable == 0) {
			return false;
		}

		out_indices.InitSize(usable);
		memcpy(out_indices.pData, source.pData, usable * sizeof(uint32));

		return true;
	}

	if (source.Size < 3) {
		return false;
	}

	out_indices.InitCapacity((source.Size - 2) * 3);

	for (uint32 i = 0; i + 2 < source.Size; i++) {
		uint32 a = source[i];
		uint32 b = source[i + 1];
		uint32 c = source[i + 2];

		if (primitive->type == cgltf_primitive_type_triangle_strip) {
			if ((i % 2) == 1) {
				std::swap(a, b);
			}
		}
		else {
			a = source[0];
			b = source[i + 1];
			c = source[i + 2];
		}

		if (a == b || b == c || a == c) {
			continue;
		}

		out_indices.Insert(a);
		out_indices.Insert(b);
		out_indices.Insert(c);
	}

	return out_indices.Size > 0;
}

void MergeInfluences(const SizedArray<uint32> (&joints)[2], const SizedArray<float32> (&weights)[2], uint32 set_count,
					 uint32 vertex_count, uint32 joint_count, SizedArray<uint32>& out_ids,
					 SizedArray<float32>& out_weights)
{
	out_ids.InitSize(static_cast<size_t>(vertex_count) * scInfluencesPerVertex);
	out_weights.InitSize(static_cast<size_t>(vertex_count) * scInfluencesPerVertex);

	struct Candidate
	{
		uint32 Joint;
		float32 Weight;
	};

	for (uint32 vertex = 0; vertex < vertex_count; vertex++) {
		Candidate candidates[2 * scInfluencesPerVertex];
		uint32 candidate_count = 0;

		for (uint32 set = 0; set < set_count; set++) {
			for (uint32 k = 0; k < scInfluencesPerVertex; k++) {
				const float32 weight = weights[set][vertex * scInfluencesPerVertex + k];
				const uint32 joint = joints[set][vertex * scInfluencesPerVertex + k];

				if (!(weight > 0.0f) || joint >= joint_count) {
					continue;
				}

				candidates[candidate_count++] = { joint, weight };
			}
		}

		const uint32 kept = std::min(candidate_count, scInfluencesPerVertex);

		for (uint32 k = 0; k < kept; k++) {
			uint32 best = k;

			for (uint32 j = k + 1; j < candidate_count; j++) {
				if (candidates[j].Weight > candidates[best].Weight) {
					best = j;
				}
			}

			std::swap(candidates[k], candidates[best]);
		}

		float32 sum = 0.0f;

		for (uint32 k = 0; k < kept; k++) {
			sum += candidates[k].Weight;
		}

		for (uint32 k = 0; k < scInfluencesPerVertex; k++) {
			const size_t slot = static_cast<size_t>(vertex) * scInfluencesPerVertex + k;

			if (sum > 1e-6f && k < kept) {
				out_ids[slot] = candidates[k].Joint;
				out_weights[slot] = candidates[k].Weight / sum;
			}
			else {
				out_ids[slot] = 0;
				out_weights[slot] = (sum <= 1e-6f && k == 0) ? 1.0f : 0.0f;
			}
		}
	}
}

bool IsIdentityMatrix(const float* m)
{
	for (uint32 i = 0; i < 16; i++) {
		const float expected = (i % 5 == 0) ? 1.0f : 0.0f;

		if (std::abs(m[i] - expected) > 1e-6f) {
			return false;
		}
	}

	return true;
}

void TransformGeometry(MeshAttributes& attributes, SizedArray<uint32>& indices, const float* m)
{
	const float a00 = m[0];
	const float a10 = m[1];
	const float a20 = m[2];
	const float a01 = m[4];
	const float a11 = m[5];
	const float a21 = m[6];
	const float a02 = m[8];
	const float a12 = m[9];
	const float a22 = m[10];

	const float c00 = a11 * a22 - a12 * a21;
	const float c01 = a12 * a20 - a10 * a22;
	const float c02 = a10 * a21 - a11 * a20;
	const float c10 = a02 * a21 - a01 * a22;
	const float c11 = a00 * a22 - a02 * a20;
	const float c12 = a01 * a20 - a00 * a21;
	const float c20 = a01 * a12 - a02 * a11;
	const float c21 = a02 * a10 - a00 * a12;
	const float c22 = a00 * a11 - a01 * a10;

	const float determinant = a00 * c00 + a01 * c01 + a02 * c02;
	const float sign = (determinant < 0.0f) ? -1.0f : 1.0f;

	for (size_t i = 0; i + 2 < attributes.Positions.Size; i += 3) {
		float* p = &attributes.Positions[i];

		const float x = p[0];
		const float y = p[1];
		const float z = p[2];

		p[0] = a00 * x + a01 * y + a02 * z + m[12];
		p[1] = a10 * x + a11 * y + a12 * z + m[13];
		p[2] = a20 * x + a21 * y + a22 * z + m[14];
	}

	for (size_t i = 0; i + 2 < attributes.Normals.Size; i += 3) {
		float* n = &attributes.Normals[i];

		const float x = n[0];
		const float y = n[1];
		const float z = n[2];

		float nx = (c00 * x + c01 * y + c02 * z) * sign;
		float ny = (c10 * x + c11 * y + c12 * z) * sign;
		float nz = (c20 * x + c21 * y + c22 * z) * sign;

		const float length = std::sqrt(nx * nx + ny * ny + nz * nz);

		if (length > 1e-20f) {
			nx /= length;
			ny /= length;
			nz /= length;
		}

		n[0] = nx;
		n[1] = ny;
		n[2] = nz;
	}

	for (size_t i = 0; i + 3 < attributes.Tangents.Size; i += 4) {
		float* t = &attributes.Tangents[i];

		const float x = t[0];
		const float y = t[1];
		const float z = t[2];

		float tx = a00 * x + a01 * y + a02 * z;
		float ty = a10 * x + a11 * y + a12 * z;
		float tz = a20 * x + a21 * y + a22 * z;

		const float length = std::sqrt(tx * tx + ty * ty + tz * tz);

		if (length > 1e-20f) {
			tx /= length;
			ty /= length;
			tz /= length;
		}

		t[0] = tx;
		t[1] = ty;
		t[2] = tz;
		t[3] *= sign;
	}

	if (determinant < 0.0f) {
		for (size_t i = 0; i + 2 < indices.Size; i += 3) {
			std::swap(indices[i + 1], indices[i + 2]);
		}
	}
}

void ApplyUvTransform(SizedArray<float32>& uvs, const cgltf_texture_transform& transform)
{
	const float cosine = std::cos(transform.rotation);
	const float sine = std::sin(transform.rotation);

	for (size_t i = 0; i + 1 < uvs.Size; i += 2) {
		const float u = uvs[i] * transform.scale[0];
		const float v = uvs[i + 1] * transform.scale[1];

		uvs[i] = cosine * u - sine * v + transform.offset[0];
		uvs[i + 1] = sine * u + cosine * v + transform.offset[1];
	}
}

int32 GetViewUvSet(const cgltf_texture_view& view)
{
	if (view.has_transform && view.transform.has_texcoord) {
		return view.transform.texcoord;
	}

	return view.texcoord;
}

bool IsSameTransform(const cgltf_texture_view& a, const cgltf_texture_view& b)
{
	if (a.has_transform != b.has_transform) {
		return false;
	}

	if (!a.has_transform) {
		return true;
	}

	const cgltf_texture_transform& ta = a.transform;
	const cgltf_texture_transform& tb = b.transform;

	return std::abs(ta.offset[0] - tb.offset[0]) < 1e-6f && std::abs(ta.offset[1] - tb.offset[1]) < 1e-6f &&
		   std::abs(ta.rotation - tb.rotation) < 1e-6f && std::abs(ta.scale[0] - tb.scale[0]) < 1e-6f &&
		   std::abs(ta.scale[1] - tb.scale[1]) < 1e-6f;
}

UvChoice ChooseUvSet(const cgltf_material* material)
{
	UvChoice choice;

	if (material == nullptr) {
		return choice;
	}

	std::vector<const cgltf_texture_view*> views;

	auto add_view = [&](const cgltf_texture_view& view)
	{
		if (view.texture != nullptr) {
			views.push_back(&view);
		}
	};

	if (material->has_pbr_specular_glossiness) {
		add_view(material->pbr_specular_glossiness.diffuse_texture);
		add_view(material->pbr_specular_glossiness.specular_glossiness_texture);
	}
	else if (material->has_pbr_metallic_roughness) {
		add_view(material->pbr_metallic_roughness.base_color_texture);
		add_view(material->pbr_metallic_roughness.metallic_roughness_texture);
	}

	add_view(material->normal_texture);
	add_view(material->occlusion_texture);
	add_view(material->emissive_texture);

	if (views.empty()) {
		return choice;
	}

	const cgltf_texture_view& primary = *views[0];

	choice.Set = static_cast<uint32>(std::max(GetViewUvSet(primary), 0));

	if (primary.has_transform) {
		choice.bHasTransform = true;
		choice.Transform = primary.transform;
	}

	for (const cgltf_texture_view* view : views) {
		if (GetViewUvSet(*view) != GetViewUvSet(primary) || !IsSameTransform(*view, primary)) {
			choice.bMismatch = true;
		}
	}

	return choice;
}

} // namespace

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

void LoaderGltf::MakeMaterialForPrimitive(Object* object, cgltf_primitive* primitive)
{
	cgltf_material* gltf_material = primitive->material;

	const bool is_skinned = object->IsSkinned();

	if (!gltf_material && !is_skinned) {
		return;
	}

	const auto cache_key = std::make_pair(static_cast<const cgltf_material*>(gltf_material), is_skinned);

	auto cached = mMaterialCache.find(cache_key);

	if (cached != mMaterialCache.end()) {
		object->SetMaterial(cached->second);
		return;
	}

	String material_name = (gltf_material && gltf_material->name) ? gltf_material->name : object->Name.Get();

	MaterialID material_id = gMaterialManager->TryNewMaterial(material_name, is_skinned);

	if (material_id.IsNull()) {
		return;
	}

	Material* material = gMaterialManager->GetMaterial(material_id);

	mMaterialCache[cache_key] = material_id;

	object->SetMaterial(material_id);

	if (!gltf_material) {
		material->Diffuse.SetTicket(gAssetManager->GetNullImageTicket(eImageFormat::RGBA8_UNorm));
		material->Finalize();
		return;
	}

	const std::pair<bool, const char*> unsupported_extensions[] = {
		{ gltf_material->has_transmission, "KHR_materials_transmission" },
		{ gltf_material->has_volume, "KHR_materials_volume" },
		{ gltf_material->has_clearcoat, "KHR_materials_clearcoat" },
		{ gltf_material->has_sheen, "KHR_materials_sheen" },
		{ gltf_material->has_iridescence, "KHR_materials_iridescence" },
		{ gltf_material->has_anisotropy, "KHR_materials_anisotropy" },
		{ gltf_material->has_diffuse_transmission, "KHR_materials_diffuse_transmission" },
		{ gltf_material->has_specular, "KHR_materials_specular" },
	};

	for (const auto& [is_present, extension_name] : unsupported_extensions) {
		if (is_present && ShouldWarn(std::string("extension:") + extension_name)) {
			LogWarning(LC_ASSET, "glTF '{}' uses {}, which is not supported and is ignored", mModelPath,
					   extension_name);
		}
	}

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

	{
		const cgltf_texture* occlusion = gltf_material->occlusion_texture.texture;
		const cgltf_texture* metallic_roughness = gltf_mr.metallic_roughness_texture.texture;

		const bool occlusion_is_packed = !use_specular_glossiness && occlusion != nullptr &&
										 metallic_roughness != nullptr &&
										 GetTextureImage(occlusion) == GetTextureImage(metallic_roughness);

		material->SetOcclusionStrength(occlusion_is_packed ? gltf_material->occlusion_texture.scale : 0.0f);

		if (occlusion != nullptr && !occlusion_is_packed && ShouldWarn("separate_occlusion")) {
			LogWarning(LC_ASSET,
					   "glTF '{}' has an occlusion texture that is not packed with the metallic-roughness "
					   "texture, it is ignored",
					   mModelPath);
		}
	}

	if (!gltf_material->unlit) {
		const float32 emissive_strength = gltf_material->has_emissive_strength
											  ? gltf_material->emissive_strength.emissive_strength
											  : 1.0f;

		material->SetEmissive(gltf_material->emissive_factor, emissive_strength);

		if (gltf_material->emissive_texture.texture != nullptr) {
			MakeMaterialTextureForPrimitive(mModelPath, material, "emissive", material->Emissive,
											gltf_material->emissive_texture);
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
			// Mask mode: opaque pipeline with an alpha test, the shaders discard below the material's cutoff
			material->SetAlphaMask(true);
			material->SetAlphaCutoff(gltf_material->alpha_cutoff);
			material->SetAlpha(baseAlpha);
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

bool LoaderGltf::BuildPrimitiveMesh(PrimitiveMesh& mesh, cgltf_primitive* primitive, const MeshNodeContext& context)
{
	const cgltf_accessor* position_accessor = nullptr;
	const cgltf_accessor* normal_accessor = nullptr;
	const cgltf_accessor* tangent_accessor = nullptr;
	const cgltf_accessor* uv_accessors[scMaxUvSets] = {};
	const cgltf_accessor* joint_accessors[2] = {};
	const cgltf_accessor* weight_accessors[2] = {};

	for (cgltf_size i = 0; i < primitive->attributes_count; i++) {
		const cgltf_attribute& attribute = primitive->attributes[i];
		const cgltf_int index = attribute.index;

		switch (attribute.type) {
		case cgltf_attribute_type_position:
			if (index == 0) {
				position_accessor = attribute.data;
			}
			break;
		case cgltf_attribute_type_normal:
			if (index == 0) {
				normal_accessor = attribute.data;
			}
			break;
		case cgltf_attribute_type_tangent:
			if (index == 0) {
				tangent_accessor = attribute.data;
			}
			break;
		case cgltf_attribute_type_texcoord:
			if (index >= 0 && index < static_cast<cgltf_int>(scMaxUvSets)) {
				uv_accessors[index] = attribute.data;
			}
			break;
		case cgltf_attribute_type_joints:
			if (index == 0 || index == 1) {
				joint_accessors[index] = attribute.data;
			}
			break;
		case cgltf_attribute_type_weights:
			if (index == 0 || index == 1) {
				weight_accessors[index] = attribute.data;
			}
			break;
		case cgltf_attribute_type_color:
			if (ShouldWarn("vertex_colors")) {
				LogWarning(LC_ASSET, "glTF '{}' has vertex colors, which are not supported and are ignored",
						   mModelPath);
			}
			break;
		default:
			break;
		}
	}

	if (primitive->targets_count > 0 && ShouldWarn("morph_targets")) {
		LogWarning(LC_ASSET, "glTF '{}' has morph targets, which are not supported and are ignored", mModelPath);
	}

	MeshAttributes attributes;

	if (!UnpackFloats(position_accessor, cgltf_type_vec3, attributes.Positions)) {
		LogError(LC_ASSET, "A primitive in '{}' has no readable positions", mModelPath);
		return false;
	}

	uint32 vertex_count = static_cast<uint32>(attributes.Positions.Size / 3);

	SizedArray<uint32> indices;

	if (!BuildTriangleIndices(primitive, vertex_count, indices)) {
		LogError(LC_ASSET, "A primitive in '{}' has no valid triangles", mModelPath);
		return false;
	}

	if (UnpackFloats(normal_accessor, cgltf_type_vec3, attributes.Normals) &&
		attributes.Normals.Size != static_cast<size_t>(vertex_count) * 3) {
		attributes.Normals.Free();
	}

	if (UnpackFloats(tangent_accessor, cgltf_type_vec4, attributes.Tangents) &&
		attributes.Tangents.Size != static_cast<size_t>(vertex_count) * 4) {
		attributes.Tangents.Free();
	}

	const UvChoice uv_choice = ChooseUvSet(primitive->material);

	if (uv_choice.bMismatch &&
		ShouldWarn(std::string("uv_mismatch:") + std::to_string(reinterpret_cast<uintptr_t>(primitive->material)))) {
		LogWarning(LC_ASSET,
				   "The textures of material '{}' in '{}' use different UV sets or transforms, only the "
				   "first one is applied",
				   primitive->material->name ? primitive->material->name : "<unnamed>", mModelPath);
	}

	uint32 uv_set = (uv_choice.Set < scMaxUvSets) ? uv_choice.Set : 0;

	if (uv_accessors[uv_set] == nullptr) {
		for (uint32 set = 0; set < scMaxUvSets; set++) {
			if (uv_accessors[set] != nullptr) {
				uv_set = set;
				break;
			}
		}
	}

	if (UnpackFloats(uv_accessors[uv_set], cgltf_type_vec2, attributes.Uvs) &&
		attributes.Uvs.Size != static_cast<size_t>(vertex_count) * 2) {
		attributes.Uvs.Free();
	}

	if (context.pSkin != nullptr && joint_accessors[0] != nullptr && weight_accessors[0] != nullptr) {
		SizedArray<uint32> joints[2];
		SizedArray<float32> weights[2];

		const size_t expected = static_cast<size_t>(vertex_count) * scInfluencesPerVertex;

		bool has_first_set = UnpackJoints(joint_accessors[0], joints[0]) &&
							 UnpackFloats(weight_accessors[0], cgltf_type_vec4, weights[0]) &&
							 joints[0].Size == expected && weights[0].Size == expected;

		const bool has_second_set = has_first_set && joint_accessors[1] != nullptr && weight_accessors[1] != nullptr &&
									UnpackJoints(joint_accessors[1], joints[1]) &&
									UnpackFloats(weight_accessors[1], cgltf_type_vec4, weights[1]) &&
									joints[1].Size == expected && weights[1].Size == expected;

		if (has_first_set) {
			MergeInfluences(joints, weights, has_second_set ? 2 : 1, vertex_count,
							static_cast<uint32>(context.pSkin->joints_count), attributes.BoneIds, attributes.Weights);
		}
	}

	if (context.bBakeTransform) {
		TransformGeometry(attributes, indices, context.WorldMatrix);
	}

	if (uv_choice.bHasTransform && !attributes.Uvs.IsEmpty()) {
		ApplyUvTransform(attributes.Uvs, uv_choice.Transform);
	}

	if (attributes.Normals.IsEmpty()) {
		MeshUtil::ExpandByIndices(attributes.Positions, 3, indices);
		MeshUtil::ExpandByIndices(attributes.Uvs, 2, indices);
		MeshUtil::ExpandByIndices(attributes.Tangents, 4, indices);
		MeshUtil::ExpandByIndices(attributes.Weights, scInfluencesPerVertex, indices);
		MeshUtil::ExpandByIndices(attributes.BoneIds, scInfluencesPerVertex, indices);

		for (uint32 i = 0; i < indices.Size; i++) {
			indices[i] = i;
		}

		vertex_count = static_cast<uint32>(attributes.Positions.Size / 3);

		MeshUtil::GenerateFlatNormals(attributes.Positions, attributes.Normals);
	}

	const bool has_normal_map = primitive->material != nullptr &&
								primitive->material->normal_texture.texture != nullptr;

	if (attributes.Tangents.IsEmpty() && !attributes.Uvs.IsEmpty() && has_normal_map) {
		MeshUtil::GenerateTangents(attributes.Positions, attributes.Normals, attributes.Uvs, indices,
								   attributes.Tangents);
	}

	// Since GLTF is stored with right handed coordinates (-x, y, z), we need to flip X when creating the vertex
	// buffers.
	constexpr eVertexCreateFlags create_flags = eVertexCreateFlags::NegativeX | eVertexCreateFlags::DefaultLayout;
	mesh.VertexList.CreateFrom(attributes.Positions, attributes.Normals, attributes.Uvs, attributes.Tangents,
							   attributes.Weights, attributes.BoneIds, create_flags);

	mesh.SetIndices(std::move(indices));

	return true;
}

bool LoaderGltf::BuildObjectsFromPrimitives(Object* container_object, cgltf_mesh* gltf_mesh,
											const MeshNodeContext& context, BBox& out_bounds)
{
	std::vector<cgltf_primitive*> primitives;

	for (cgltf_size i = 0; i < gltf_mesh->primitives_count; i++) {
		cgltf_primitive* gltf_primitive = &gltf_mesh->primitives[i];

		if (IsPrimitiveSupported(gltf_primitive)) {
			primitives.push_back(gltf_primitive);
		}
		else if (ShouldWarn("primitive_topology")) {
			LogWarning(LC_ASSET, "glTF '{}' has points or lines, only triangles are supported and the rest are skipped",
					   mModelPath);
		}
	}

	// Similarly to `CreateGpuResource`, we are going to make the `object` into a container
	// if there are multiple primitives.
	const bool has_multiple_primitives = primitives.size() > 1;

	bool has_bounds = false;

	for (cgltf_primitive* gltf_primitive : primitives) {
		Ref<PrimitiveMesh> primitive_mesh = Ref<PrimitiveMesh>::New();

		// Keep the primitive mesh's vertices and indices in memory if `KeepInMemory` is set
		primitive_mesh->bKeepInMemory = bKeepInMemory;

		if (!BuildPrimitiveMesh(*primitive_mesh, gltf_primitive, context)) {
			LogError(LC_ASSET, "Skipping a primitive of mesh '{}' in '{}'",
					 gltf_mesh->name ? gltf_mesh->name : "<unnamed>", mModelPath);
			continue;
		}

		Object* current_object = container_object;

		if (has_multiple_primitives) {
			current_object = gObjectManager->NewObject(container_object->Name.Get(), MaterialID::scNull,
													   eObjectTag::None, true);
		}

		current_object->pMesh = primitive_mesh;
		current_object->Bounds = MeshUtil::CalculateBounds(primitive_mesh->VertexList);

		MakeMaterialForPrimitive(current_object, gltf_primitive);

		if (has_bounds) {
			out_bounds.Min = Vec3f::Min(out_bounds.Min, current_object->Bounds.Min);
			out_bounds.Max = Vec3f::Max(out_bounds.Max, current_object->Bounds.Max);
		}
		else {
			out_bounds = current_object->Bounds;
			has_bounds = true;
		}

		if (has_multiple_primitives) {
			// Attach the current object to the object container (our output)
			AttachChild(container_object, current_object->ID);
		}
	}

	if (has_multiple_primitives && has_bounds) {
		container_object->Bounds = out_bounds;
	}

	return has_bounds;
}


void LoaderGltf::UploadMeshToGpu(Object* object)
{
	if (object->pMesh.IsValid()) {
		Ref<PrimitiveMesh> primitive_mesh = object->pMesh;

		// Set the mesh indices
		primitive_mesh->UploadIndices(GraphicsBackendFwd::GetUploadCmd());
		primitive_mesh->UploadVertices(GraphicsBackendFwd::GetUploadCmd());

		primitive_mesh->bIsReady = true;
	}
}

void LoaderGltf::UploadObjectTree(Object* object)
{
	if (object == nullptr) {
		return;
	}

	UploadMeshToGpu(object);

	for (ObjectID child_id : object->AttachedNodes) {
		UploadObjectTree(gObjectManager->GetObject(child_id));
	}
}

std::vector<int32> LoaderGltf::BuildJointLookup(cgltf_skin* skin) const
{
	std::vector<int32> lookup(mpGltfData->nodes_count, BoneNull);

	for (cgltf_size i = 0; i < skin->joints_count; i++) {
		lookup[skin->joints[i] - mpGltfData->nodes] = static_cast<int32>(i);
	}

	return lookup;
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


void LoaderGltf::LoadSkeleton(Skeleton& skel, cgltf_skin* skin, const std::vector<int32>& joint_lookup)
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
	skel.InvBindTransforms.InitSize(joint_count);

	if (skin->inverse_bind_matrices) {
		cgltf_accessor* accessor = skin->inverse_bind_matrices;

		cgltf_accessor_unpack_floats(accessor, reinterpret_cast<float32*>(skel.InvBindTransforms.pData),
									 joint_count * 16);

		for (uint32 i = 0; i < joint_count; i++) {
			Mat4f& m = skel.InvBindTransforms[i];

			m = reflection * m * reflection;
		}
	}
	else {
		for (uint32 i = 0; i < joint_count; i++) {
			skel.InvBindTransforms[i] = Mat4f::scIdentity;
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
		skel.ParentIndices[i] = joint->parent ? static_cast<uint32>(joint_lookup[joint->parent - mpGltfData->nodes])
											  : BoneNull;
		skel.RestPose[i] = MakeRestPoseForJoint(joint);

		const bool is_root_joint = (skel.ParentIndices[i] == BoneNull);

		skel.RootTransforms[i] = Mat4f::scIdentity;

		if (is_root_joint && joint->parent) {
			Mat4f node_world;
			cgltf_node_transform_world(joint->parent, reinterpret_cast<float32*>(&node_world));

			skel.RootTransforms[i] = reflection * node_world * reflection;
		}
	}

	skel.BuildEvaluationOrder();
}

void LoaderGltf::LoadAnimation(Animation& out_anim, const cgltf_animation& anim, cgltf_skin* skin,
							   const std::vector<int32>& joint_lookup)
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

		if (!skin || !channel->target_node || sampler == nullptr) {
			continue;
		}

		const int32 joint_idx = joint_lookup[channel->target_node - mpGltfData->nodes];

		if (joint_idx < 0) {
			continue;
		}

		const cgltf_animation_path_type path = channel->target_path;

		if (path != cgltf_animation_path_type_translation && path != cgltf_animation_path_type_rotation &&
			path != cgltf_animation_path_type_scale) {
			if (ShouldWarn("animation_path")) {
				LogWarning(LC_ASSET,
						   "glTF '{}' animates something other than joint translation, rotation or scale, "
						   "which is not supported",
						   mModelPath);
			}

			continue;
		}

		const cgltf_type expected_type = (path == cgltf_animation_path_type_rotation) ? cgltf_type_vec4
																					  : cgltf_type_vec3;

		const bool is_cubic = sampler->interpolation == cgltf_interpolation_type_cubic_spline;
		const cgltf_size key_count = sampler->input->count;

		if (sampler->input->type != cgltf_type_scalar || sampler->output->type != expected_type ||
			sampler->output->count < key_count * (is_cubic ? 3 : 1)) {
			LogWarning(LC_ASSET, "Skipping an animation channel in '{}' with mismatched accessors", mModelPath);
			continue;
		}

		BoneTrack& joint_track = out_anim.BoneTracks.pData[joint_idx];

		const cgltf_size num_components = cgltf_num_components(expected_type);

		SizedArray<float32> times;
		times.InitSize(key_count);
		cgltf_accessor_unpack_floats(sampler->input, times.pData, key_count);

		if (key_count > 0) {
			out_anim.Duration = std::max(out_anim.Duration, times.pData[key_count - 1]);
		}

		eAnimationInterpolation interpolation = eAnimationInterpolation::Linear;

		if (sampler->interpolation == cgltf_interpolation_type_step) {
			interpolation = eAnimationInterpolation::Step;
		}
		else if (is_cubic) {
			interpolation = eAnimationInterpolation::CubicSpline;
		}

		const cgltf_size stride = is_cubic ? 3 : 1;
		const cgltf_size value_offset = is_cubic ? 1 : 0;

		auto read_element = [&](cgltf_size key_index, cgltf_size element, float32* buffer)
		{
			buffer[3] = 0.0f;
			cgltf_accessor_read_float(sampler->output, key_index * stride + element, buffer, num_components);

			if (path == cgltf_animation_path_type_translation) {
				buffer[0] = -buffer[0];
			}
			else if (path == cgltf_animation_path_type_rotation) {
				buffer[1] = -buffer[1];
				buffer[2] = -buffer[2];
			}
		};

		if (path == cgltf_animation_path_type_translation) {
			joint_track.Translation.Times = std::move(times);
			joint_track.Translation.Interpolation = interpolation;
			joint_track.Translation.Values.InitSize(key_count);

			if (is_cubic) {
				joint_track.Translation.InTangents.InitSize(key_count);
				joint_track.Translation.OutTangents.InitSize(key_count);
			}

			for (uint32 key_index = 0; key_index < key_count; key_index++) {
				float32 buffer[4];

				read_element(key_index, value_offset, buffer);
				joint_track.Translation.Values[key_index] = Vec3f(buffer);

				if (is_cubic) {
					read_element(key_index, 0, buffer);
					joint_track.Translation.InTangents[key_index] = Vec3f(buffer);

					read_element(key_index, 2, buffer);
					joint_track.Translation.OutTangents[key_index] = Vec3f(buffer);
				}
			}
		}
		else if (path == cgltf_animation_path_type_rotation) {
			joint_track.Rotation.Times = std::move(times);
			joint_track.Rotation.Interpolation = interpolation;
			joint_track.Rotation.Values.InitSize(key_count);

			if (is_cubic) {
				joint_track.Rotation.InTangents.InitSize(key_count);
				joint_track.Rotation.OutTangents.InitSize(key_count);
			}

			for (uint32 key_index = 0; key_index < key_count; key_index++) {
				float32 buffer[4];

				read_element(key_index, value_offset, buffer);
				joint_track.Rotation.Values[key_index] = Quat(buffer);

				if (is_cubic) {
					read_element(key_index, 0, buffer);
					joint_track.Rotation.InTangents[key_index] = Quat(buffer);

					read_element(key_index, 2, buffer);
					joint_track.Rotation.OutTangents[key_index] = Quat(buffer);
				}
			}
		}
		else {
			joint_track.Scale.Times = std::move(times);
			joint_track.Scale.Interpolation = interpolation;
			joint_track.Scale.Values.InitSize(key_count);

			if (is_cubic) {
				joint_track.Scale.InTangents.InitSize(key_count);
				joint_track.Scale.OutTangents.InitSize(key_count);
			}

			for (uint32 key_index = 0; key_index < key_count; key_index++) {
				float32 buffer[4];

				read_element(key_index, value_offset, buffer);
				joint_track.Scale.Values[key_index] = Vec3f(buffer);

				if (is_cubic) {
					read_element(key_index, 0, buffer);
					joint_track.Scale.InTangents[key_index] = Vec3f(buffer);

					read_element(key_index, 2, buffer);
					joint_track.Scale.OutTangents[key_index] = Vec3f(buffer);
				}
			}
		}
	}

	LogInfo(LC_ASSET, "Loaded animation '{}': {:.3f}s, {} joints", out_anim.Name, out_anim.Duration, joint_count);
}

void LoaderGltf::LoadAnimations(Skeleton& skel, cgltf_skin* skin, const std::vector<int32>& joint_lookup)
{
	if (!mpGltfData->animations_count || !skin) {
		return;
	}

	skel.Animations.InitCapacity(mpGltfData->animations_count);

	for (uint32 i = 0; i < mpGltfData->animations_count; i++) {
		Animation anim;
		LoadAnimation(anim, mpGltfData->animations[i], skin, joint_lookup);
		skel.Animations.Insert(std::move(anim));
	}

	// Loop the first animation by default. The owner can swap it out with SetRestAnimation(), or pass null to hold the
	// rest pose.
	skel.SetRestAnimation(&skel.Animations[0]);

	LogInfo(LC_ASSET, "Loaded {} animations", mpGltfData->animations_count);
}

bool LoaderGltf::ShouldWarn(const std::string& key) { return mWarned.insert(key).second; }

bool LoaderGltf::ValidateData()
{
	const char* model_name = mModelPath.IsEmpty() ? "<memory>" : mModelPath.CStr();

	if (cgltf_validate(mpGltfData) != cgltf_result_success) {
		LogError(LC_ASSET, "GLTF file '{}' failed validation", model_name);
		return false;
	}

	for (cgltf_size i = 0; i < mpGltfData->skins_count; i++) {
		const cgltf_skin& skin = mpGltfData->skins[i];

		if (skin.joints_count == 0) {
			LogError(LC_ASSET, "GLTF file '{}' has a skin without joints", model_name);
			return false;
		}

		if (skin.inverse_bind_matrices != nullptr && (skin.inverse_bind_matrices->type != cgltf_type_mat4 ||
													  skin.inverse_bind_matrices->count < skin.joints_count)) {
			LogError(LC_ASSET, "GLTF file '{}' has a skin with too few inverse bind matrices", model_name);
			return false;
		}
	}

	return true;
}

void LoaderGltf::ProcessData(AssetTicket& ticket)
{
	// If there is only one mesh to load, store the mesh directly in the output object
	Object* output_object = static_cast<Object*>(ticket.Get());

	mpRootObject = output_object;

	std::vector<cgltf_node*> mesh_nodes;

	for (cgltf_size node_index = 0; node_index < mpGltfData->nodes_count; node_index++) {
		cgltf_node* node = &mpGltfData->nodes[node_index];

		// Only nodes that reference a mesh become objects. Skinned models can have hundreds of joint nodes (and other
		// transform-only nodes); giving each of those an object quickly exhausts the object pool.
		if (node->mesh) {
			mesh_nodes.push_back(node);
		}
	}

	if (mesh_nodes.empty()) {
		LogWarning(LC_ASSET, "GLTF file '{}' has no nodes that use a mesh", mModelPath);
		return;
	}

	std::vector<bool> is_joint(mpGltfData->nodes_count, false);

	for (cgltf_size skin_index = 0; skin_index < mpGltfData->skins_count; skin_index++) {
		const cgltf_skin& skin = mpGltfData->skins[skin_index];

		for (cgltf_size joint_index = 0; joint_index < skin.joints_count; joint_index++) {
			is_joint[skin.joints[joint_index] - mpGltfData->nodes] = true;
		}
	}

	// If there are multiple gltf meshes, we will need to use the output object as a
	// container for multiple other meshes
	const bool has_multiple_meshes = mesh_nodes.size() > 1;

	int32 attach_id = 1;
	uint32 joint_attached_count = 0;

	bool has_root_bounds = false;
	BBox root_bounds;

	// Skeletons (and their animations) built so far, indexed by skin. Meshes skinned to the same skin share one
	// skeleton rather than each parsing their own copy.
	std::vector<Ref<Skeleton>> skeletons(mpGltfData->skins_count);

	// Load each mesh node in the GLTF as a new separate object.
	for (cgltf_node* node : mesh_nodes) {
		MeshNodeContext context;
		context.pSkin = node->skin;

		if (node->skin == nullptr) {
			bool has_joint_ancestor = false;

			for (const cgltf_node* ancestor = node->parent; ancestor != nullptr; ancestor = ancestor->parent) {
				if (is_joint[ancestor - mpGltfData->nodes]) {
					has_joint_ancestor = true;
					break;
				}
			}

			if (has_joint_ancestor) {
				joint_attached_count++;
			}
			else {
				cgltf_node_transform_world(node, context.WorldMatrix);
				context.bBakeTransform = !IsIdentityMatrix(context.WorldMatrix);
			}
		}

		Object* current_object = output_object;

		// If there are multiple meshes, each one gets its own object that is attached to the output object.
		if (has_multiple_meshes) {
			current_object = gObjectManager->NewObject(std::format("{}_{}", output_object->Name.Get(), attach_id++),
													   MaterialID::scNull, eObjectTag::None, true);
		}

		// Load all meshes from the node we are currently on.
		BBox node_bounds;
		const bool has_node_bounds = BuildObjectsFromPrimitives(current_object, node->mesh, context, node_bounds);

		// If the node has a skeleton, load it in.
		if (node->skin) {
			Ref<Skeleton>& skeleton = skeletons[node->skin - mpGltfData->skins];

			if (!skeleton) {
				const std::vector<int32> joint_lookup = BuildJointLookup(node->skin);

				skeleton = Ref<Skeleton>::New();
				LoadSkeleton(*skeleton, node->skin, joint_lookup);
				LoadAnimations(*skeleton, node->skin, joint_lookup);
			}

			current_object->pSkeleton = skeleton;

			// A mesh with multiple primitives is split into attached objects, which each draw skinned vertices and
			// need the skeleton themselves.
			for (ObjectID primitive_id : GetChildren(current_object)) {
				gObjectManager->GetObject(primitive_id)->pSkeleton = skeleton;
			}
		}

		if (has_multiple_meshes) {
			// Attach the loaded object onto the final object. This will be a container.
			AttachChild(output_object, current_object->ID);

			if (has_node_bounds) {
				if (has_root_bounds) {
					root_bounds.Min = Vec3f::Min(root_bounds.Min, node_bounds.Min);
					root_bounds.Max = Vec3f::Max(root_bounds.Max, node_bounds.Max);
				}
				else {
					root_bounds = node_bounds;
					has_root_bounds = true;
				}
			}
		}
	}

	if (has_root_bounds) {
		output_object->Bounds = root_bounds;
	}

	if (joint_attached_count > 0) {
		LogInfo(LC_ASSET,
				"GLTF file '{}': {} mesh nodes are attached to skeleton joints and are drawn in the "
				"model's space without their node transforms",
				mModelPath, joint_attached_count);
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

	if (!ValidateData()) {
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

	status = cgltf_load_buffers(&options, mpGltfData, nullptr);
	if (status != cgltf_result_success) {
		LogError(LC_ASSET, "Error loading buffers from GLTF data");
		return eLoaderStatus::Error;
	}

	if (!ValidateData()) {
		return eLoaderStatus::Error;
	}

	ProcessData(ticket);

	return eLoaderStatus::Success;
}

void LoaderGltf::CreateGpuResource(AssetTicket& ticket)
{
	Object* object = static_cast<Object*>(ticket.Get());

	for (ObjectID child_id : mRootChildren) {
		UploadObjectTree(gObjectManager->GetObject(child_id));
	}

	UploadMeshToGpu(object);
	ticket.SignalUploadedToGpu();
}

void LoaderGltf::AttachChild(Object* parent, ObjectID child_id)
{
	if (parent == mpRootObject) {
		mRootChildren.push_back(child_id);
		return;
	}

	parent->AttachObject(child_id);
}

std::vector<ObjectID> LoaderGltf::GetChildren(Object* parent) const
{
	if (parent == mpRootObject) {
		return mRootChildren;
	}

	std::vector<ObjectID> children;

	for (ObjectID child_id : parent->AttachedNodes) {
		children.push_back(child_id);
	}

	return children;
}

void LoaderGltf::Publish(Object* root)
{
	for (ObjectID child_id : mRootChildren) {
		root->AttachObject(child_id);
	}

	mRootChildren.clear();
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
