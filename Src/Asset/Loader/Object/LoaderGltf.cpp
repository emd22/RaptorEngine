#include "LoaderGltf.hpp"

#include "../Image/LoaderKtx.hpp"
#include "../Image/LoaderStb.hpp"

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
#include <Util/RustInterop.hpp>
#include <cmath>

namespace fx {

using namespace renderer;

namespace loader {

template <typename TType>
static SizedArray<TType> CopyOfArray(const TType* data, size_t count)
{
	SizedArray<TType> out;

	if (data != nullptr && count != 0) {
		out.InitSize(count);
		std::memcpy(out.pData, data, count * sizeof(TType));
	}

	return out;
}

void LoaderGltf::UnpackMeshAttributes(Ref<PrimitiveMesh>& mesh, const RxGltfPrimitive& primitive, bool is_skinned)
{
	SizedArray<float32> positions = CopyOfArray(primitive.positions, primitive.position_floats);
	SizedArray<float32> normals = CopyOfArray(primitive.normals, primitive.normal_floats);
	SizedArray<float32> uvs = CopyOfArray(primitive.uvs, primitive.uv_floats);
	SizedArray<float32> tangents = CopyOfArray(primitive.tangents, primitive.tangent_floats);
	SizedArray<float32> weights;
	SizedArray<uint32> boneids;

	if (is_skinned) {
		weights = CopyOfArray(primitive.weights, primitive.weight_floats);
		boneids = CopyOfArray(primitive.joints, primitive.joint_values);
	}

	// Since GLTF is stored with right handed coordinates (-x, y, z), we need to flip X when creating the vertex
	// buffers.
	constexpr eVertexCreateFlags create_flags = eVertexCreateFlags::NegativeX;
	mesh->VertexList.CreateFrom(positions, normals, uvs, tangents, weights, boneids, create_flags);
}

static constexpr uint8 scKtx2Magic[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };

static bool HasKtx2Magic(const uint8* data, size_t size)
{
	return data != nullptr && size >= sizeof(scKtx2Magic) && memcmp(data, scKtx2Magic, sizeof(scKtx2Magic)) == 0;
}

/**
 * @brief Opens the KTX2 image that a GLTF image refers to, wherever the file keeps its bytes: in a buffer view, inlined
 * as a base64 `data:` entry or as an external file relative to the model
 */
static bool OpenGltfKtxImage(loader::LoaderKtx& ktx, const RxGltf* gltf, int32 image_index)
{
	RxGltfImage image {};

	if (!rx_gltf_image(gltf, static_cast<uint32>(image_index), &image)) {
		LogError(LC_ASSET, "Could not open glTF texture '{}'",
				 rx_gltf_image_name(gltf, static_cast<uint32>(image_index)));
		return false;
	}

	return HasKtx2Magic(image.bytes, image.size) && ktx.OpenFromMemory(image.bytes, static_cast<uint32>(image.size));
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
static bool MakeMaterialTextureForPrimitive(const RxGltf* gltf, const String& model_path, Material* material,
											const char* component_name, MaterialComponent& component,
											const RxGltfTexture& texture_view, bool* out_has_alpha = nullptr)
{
	if (texture_view.image < 0) {
		return false;
	}

	loader::LoaderKtx ktx {};

	if (!OpenGltfKtxImage(ktx, gltf, texture_view.image) || !ktx.IsOpen()) {
		LogError(LC_ASSET, "glTF textures must be KTX2 (image '{}', {} of material '{}' in '{}')",
				 rx_gltf_image_name(gltf, static_cast<uint32>(texture_view.image)), component_name,
				 material->Name.Get(), model_path);
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
		LogWarning(LC_ASSET, "glTF texture '{}' ({}) is stored as {}, expected {}", rx_gltf_image_name(gltf, static_cast<uint32>(texture_view.image)), component_name,
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

void LoaderGltf::MakeMaterialForPrimitive(Object* object, const RxGltfPrimitive& primitive)
{
	if (primitive.material < 0) {
		return;
	}

	const RxGltfMaterial& gltf_material = *rx_gltf_material(mpGltf, static_cast<uint32>(primitive.material));

	String material_name = (gltf_material.name) ? gltf_material.name : object->Name.Get();


	MaterialID material_id = gMaterialManager->NewMaterial(material_name, object->IsSkinned());
	Material* material = gMaterialManager->GetMaterial(material_id);

	object->SetMaterial(material_id);

	// KHR_materials_pbrSpecularGlossiness takes priority when present; any pbrMetallicRoughness block alongside it is
	// only a fallback for loaders that don't support the extension.
	const bool use_specular_glossiness = gltf_material.has_specular_glossiness != 0;

	// Diffuse lives in whichever PBR block the material uses
	const RxGltfTexture& diffuse_view = use_specular_glossiness ? gltf_material.diffuse_texture
																: gltf_material.base_color_texture;

	// Only blended materials care whether the texture has alpha, and finding out means scanning every texel
	const bool is_blended = (gltf_material.alpha_mode == RX_GLTF_ALPHA_BLEND);
	bool diffuse_has_alpha = false;
	bool* out_diffuse_has_alpha = is_blended ? &diffuse_has_alpha : nullptr;

	const bool has_diffuse = MakeMaterialTextureForPrimitive(mpGltf, mModelPath, material, "diffuse",
															 material->Diffuse, diffuse_view, out_diffuse_has_alpha);

	if (!has_diffuse) {
		material->Diffuse.SetTicket(gAssetManager->GetNullImageTicket(eImageFormat::RGBA8_UNorm));
	}

	if (use_specular_glossiness) {
		material->SetBaseColorFactor(gltf_material.diffuse_factor);
	}
	else {
		material->SetBaseColorFactor(gltf_material.base_color_factor);
	}

	// Load the normalmap
	MakeMaterialTextureForPrimitive(mpGltf, mModelPath, material, "normal", material->NormalMap,
									gltf_material.normal_texture);

	// Load the surface texture and factors
	if (use_specular_glossiness) {
		material->SetSpecularGlossiness(gltf_material.specular_factor, gltf_material.glossiness_factor);

		MakeMaterialTextureForPrimitive(mpGltf, mModelPath, material, "specular-glossiness",
										material->MetallicRoughness, gltf_material.specular_glossiness_texture);
	}
	else {
		material->SetMetallicRoughness(gltf_material.metallic_factor, gltf_material.roughness_factor);

		MakeMaterialTextureForPrimitive(mpGltf, mModelPath, material, "metallic-roughness",
										material->MetallicRoughness, gltf_material.metallic_roughness_texture);
	}

	material->SetOcclusionStrength(gltf_material.has_packed_occlusion ? gltf_material.packed_occlusion_strength : 0.0f);

	// Handle glTF alpha mode / baseColorFactor alpha
	{
		const float baseAlpha = use_specular_glossiness ? gltf_material.diffuse_factor[3]
														: gltf_material.base_color_factor[3];

		if (gltf_material.alpha_mode == RX_GLTF_ALPHA_BLEND) {
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
		else if (gltf_material.alpha_mode == RX_GLTF_ALPHA_MASK) {
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
		if (gltf_material.double_sided && gltf_material.alpha_mode != RX_GLTF_ALPHA_OPAQUE) {
			material->SetDoubleSided(true);
		}

		if (gltf_material.unlit) {
			material->SetUnlit(true);
		}
	}

	material->Finalize();
}

void LoaderGltf::BuildObjectsFromPrimitives(Object* container_object, uint32 mesh_index, bool is_skinned)
{
	const uint32 primitive_count = rx_gltf_primitive_count(mpGltf, mesh_index);
	const bool has_multiple_primitives = primitive_count > 1;

	// Assume at first that there is only one primitive;
	Object* current_object = container_object;

	// Similarly to `CreateGpuResource`, we are going to make the `object` into a container
	// if there are multiple primitives.
	if (has_multiple_primitives) {
		current_object = gObjectManager->NewObject(container_object->Name.Get(), MaterialID::scNull);
	}

	bool needs_new_object = false;

	for (uint32 i = 0; i < primitive_count; i++) {
		if (needs_new_object) {
			// Create a new object to load into next
			current_object = gObjectManager->NewObject(container_object->Name.Get(), MaterialID::scNull);
			needs_new_object = false;
		}


		const RxGltfPrimitive& gltf_primitive = *rx_gltf_primitive(mpGltf, mesh_index, i);
		Ref<PrimitiveMesh> primitive_mesh = Ref<PrimitiveMesh>::New();

		// Keep the primitive mesh's vertices and indices in memory if `KeepInMemory` is set
		primitive_mesh->bKeepInMemory = bKeepInMemory;

		// Load the indices in from the mesh
		if (gltf_primitive.has_indices) {
			primitive_mesh->SetIndices(CopyOfArray(gltf_primitive.indices, gltf_primitive.index_count));
		}

		UnpackMeshAttributes(primitive_mesh, gltf_primitive, is_skinned);
		current_object->pMesh = primitive_mesh;
		current_object->Bounds = MeshUtil::CalculateBounds(primitive_mesh->GetVertices());

		MakeMaterialForPrimitive(current_object, gltf_primitive);

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


void LoaderGltf::ProcessData(AssetTicket& ticket)
{
	// If there is only one mesh to load, store the mesh directly in the output object
	Object* output_object = static_cast<Object*>(ticket.Get());

	// If there are multiple gltf meshes, we will need to use the output object as a
	// container for multiple other meshes
	const uint32 node_count = rx_gltf_node_count(mpGltf);

	const bool has_multiple_meshes = rx_gltf_mesh_count(mpGltf) > 1;

	int32 attach_id = 1;

	// Skeletons (and their animations) built so far, indexed by skin. Meshes skinned to the same skin share one
	// skeleton rather than each parsing their own copy.
	std::vector<Ref<Skeleton>> skeletons(rx_gltf_skin_count(mpGltf));

	// Load each mesh node in the GLTF as a new separate object. Only nodes that reference a mesh are listed: skinned
	// models can have hundreds of joint nodes (and other transform-only nodes), and giving each of those an object
	// would quickly exhaust the object pool.
	for (uint32 node_index = 0; node_index < node_count; node_index++) {
		const RxGltfNode& node = *rx_gltf_node(mpGltf, node_index);

		Object* current_object = output_object;

		// If there are multiple meshes, each one gets its own object that is attached to the output object.
		if (has_multiple_meshes) {
			current_object = gObjectManager->NewObject(std::format("{}_{}", output_object->Name.Get(), attach_id++),
													   MaterialID::scNull);
		}

		// Load all meshes from the node we are currently on.
		BuildObjectsFromPrimitives(current_object, node.mesh, node.skin >= 0);

		// If the node has a skeleton, load it in.
		if (node.skin >= 0) {
			Ref<Skeleton>& skeleton = skeletons[node.skin];

			if (!skeleton) {
				skeleton = Ref<Skeleton>::New();
				skeleton->Adopt(rx_gltf_skeleton_new(mpGltf, static_cast<uint32>(node.skin)));
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

static const RxLogSink scGltfLog = { .user = nullptr, .log = RustInterop::Log };

eLoaderStatus LoaderGltf::Load(AssetTicket& ticket, const String& path)
{
	mModelPath = path;

	mpGltf = rx_gltf_load_file(path.CStr(), &scGltfLog);

	if (mpGltf == nullptr) {
		LogError(LC_ASSET, "Error loading GLTF file! (path: {})", path);
		return eLoaderStatus::Error;
	}

	ProcessData(ticket);

	return eLoaderStatus::Success;
}


eLoaderStatus LoaderGltf::Load(AssetTicket& ticket, const uint8* data, uint32 size)
{
	mpGltf = rx_gltf_load_memory(data, size, &scGltfLog);

	if (mpGltf == nullptr) {
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
	rx_gltf_free(mpGltf);
	mpGltf = nullptr;
}

} // namespace loader

} // namespace fx
