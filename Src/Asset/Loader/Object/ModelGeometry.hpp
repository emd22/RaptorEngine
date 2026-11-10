/*
 * File:        ModelGeometry.hpp
 * Author:      emd22
 * Created:     07/11/2026
 * Description: CPU-only mesh reading shared by the glTF/.rxmodel loaders and the map compiler
 */

#pragma once

#include "RxModelFormat.hpp"

#include <ThirdParty/cgltf.h>

#include <Core/DynArray.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/FVec3.hpp>

namespace fx::loader {

/**
 * @brief Transforms positions, normals and tangents by an affine matrix laid out as cgltf and Mat4f::RawData store it
 * (translation in elements 12-14). Normals use the cofactor matrix, and a mirroring matrix flips the winding and the
 * tangent handedness.
 */
void TransformModelGeometry(SizedArray<float32>& positions, SizedArray<float32>& normals, SizedArray<float32>& tangents,
							SizedArray<uint32>& indices, const float32* m);

bool IsIdentityMatrix(const float32* m);

namespace gltf {

struct UvChoice
{
	uint32 Set = 0;
	bool bHasTransform = false;
	cgltf_texture_transform Transform {};
	bool bMismatch = false;
};

constexpr uint32 scMaxUvSets = 4;

bool UnpackFloats(const cgltf_accessor* accessor, cgltf_type expected_type, SizedArray<float32>& out);
bool IsPrimitiveSupported(const cgltf_primitive* primitive);
bool BuildTriangleIndices(const cgltf_primitive* primitive, uint32 vertex_count, SizedArray<uint32>& out_indices);
void ApplyUvTransform(SizedArray<float32>& uvs, const cgltf_texture_transform& transform);

/// The UV set and transform of the material's first texture, which the loader applies to every texture
UvChoice ChooseUvSet(const cgltf_material* material);

} // namespace gltf

namespace rx {

struct AttributeView
{
	bool IsValid() const { return pData != nullptr; }

	const uint8* pData = nullptr;
	uint32 Stride = 0;
	uint8 Format = 0;
};

constexpr uint32 scMaxWeightSets = 2;

struct MeshViews
{
	const MeshHeader* pHeader = nullptr;
	const Submesh* pSubmeshes = nullptr;
	uint32 SubmeshCount = 0;

	const uint8* pIndices = nullptr;
	uint32 IndexSize = 0;

	AttributeView Position;
	AttributeView Normal;
	AttributeView Tangent;
	AttributeView TexCoord;
	AttributeView Joints[scMaxWeightSets];
	AttributeView Weights[scMaxWeightSets];

	bool bHasColors = false;
};

void ReadFloats(const AttributeView& view, uint32 vertex, float32* out);
bool IsFormatAllowed(uint8 semantic, uint8 format);
void ReadAttribute(const AttributeView& view, uint32 base_vertex, uint32 vertex_count, uint32 components,
				   SizedArray<float32>& out);

/// Reads the indices of a submesh relative to its base vertex. Fails if one is outside the submesh's vertex range.
bool ReadSubmeshIndices(const MeshViews& views, const Submesh& submesh, SizedArray<uint32>& out_indices);

/// Finds the header, submeshes, index buffer and vertex attributes of a MESH file and checks that they fit together
bool ReadMeshViews(const RxBinaryFile& mesh_file, MeshViews& out_views, String& out_error);

} // namespace rx

struct StaticModelMaterial
{
	String Name;

	FVec3 BaseColor = FVec3(1.0f);
	float32 Metallic = 0.0f;
	bool bHasMetallicTexture = false;

	/// File path of the base colour KTX2 image, base-dir relative, or empty
	String DiffusePath;
	/// Base colour KTX2 bytes when the image is embedded in a .glb
	SizedArray<uint8> DiffuseData;

	bool bBlend = false;
};

struct StaticModelMesh
{
	uint32 Material = 0;

	/// Model space in engine coordinates (glTF X is already negated). Normals are always present.
	SizedArray<float32> Positions;
	SizedArray<float32> Normals;
	/// The UVs the base colour texture samples, or empty
	SizedArray<float32> Uvs;
	SizedArray<uint32> Indices;
};

struct StaticModel
{
	/// Holds strings, so it is sized once from the file's material count
	SizedArray<StaticModelMaterial> Materials;
	DynArray<StaticModelMesh, GrowthFunctions::Double> Meshes;

	/// Skinned meshes and meshes that follow a joint, which only have a pose once animated
	uint32 SkippedMeshCount = 0;
};

/**
 * @brief Reads the static meshes of a .glb/.gltf or .rxmodel file as the engine would place them in the model's space
 * (node transforms and the .rxmodel root baked in). Skinned and joint-attached meshes are skipped, as is every LOD
 * but 0.
 */
bool ReadStaticModel(const String& path, StaticModel& out_model, String& out_error);

} // namespace fx::loader
