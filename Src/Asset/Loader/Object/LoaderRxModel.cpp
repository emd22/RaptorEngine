#include "LoaderRxModel.hpp"

#include "../Image/LoaderKtx.hpp"
#include "RxModelFormat.hpp"

#include <Asset/Animation.hpp>
#include <Asset/AssetBase.hpp>
#include <Asset/AssetManager.hpp>
#include <Core/Ref.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Object/Object.hpp>
#include <Renderer/Backend/GraphicsBackendFwd.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/MeshUtil.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>

namespace fx {

using namespace renderer;

namespace loader {

namespace {

constexpr uint32 scInfluencesPerVertex = 4;
constexpr uint32 scMaxWeightSets = 2;

constexpr uint8 scKtx2Magic[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };

struct AttributeView
{
	bool IsValid() const { return pData != nullptr; }

	const uint8* pData = nullptr;
	uint32 Stride = 0;
	uint8 Format = 0;
};

struct MeshSource
{
	const rx::MeshHeader* pHeader = nullptr;
	const rx::Submesh* pSubmeshes = nullptr;

	const uint8* pIndices = nullptr;
	uint32 IndexSize = 0;

	AttributeView Position;
	AttributeView Normal;
	AttributeView Tangent;
	AttributeView TexCoord;
	AttributeView Joints[scMaxWeightSets];
	AttributeView Weights[scMaxWeightSets];

	uint32 JointCount = 0;
	uint32 JointOffset = 0;
};

struct BuiltSubmesh
{
	const rx::Submesh* pSubmesh = nullptr;
	Ref<PrimitiveMesh> pMesh { nullptr };
};

float32 HalfToFloat(uint16 half)
{
	const uint32 sign = static_cast<uint32>(half & 0x8000u) << 16;
	const uint32 exponent = (half >> 10) & 0x1Fu;
	uint32 mantissa = half & 0x3FFu;

	uint32 bits = sign;

	if (exponent == 0) {
		if (mantissa != 0) {
			uint32 shift = 0;

			do {
				++shift;
				mantissa <<= 1;
			} while ((mantissa & 0x400u) == 0);

			bits = sign | ((113u - shift) << 23) | ((mantissa & 0x3FFu) << 13);
		}
	}
	else if (exponent == 31) {
		bits = sign | 0x7F800000u | (mantissa << 13);
	}
	else {
		bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
	}

	float32 value;
	memcpy(&value, &bits, sizeof(value));

	return value;
}

void ReadFloats(const AttributeView& view, uint32 vertex, float32* out)
{
	const uint8* src = view.pData + static_cast<size_t>(vertex) * view.Stride;
	const uint32 components = rx::GetFormatComponents(view.Format);

	switch (view.Format) {
	case rx::VF_F32x1:
	case rx::VF_F32x2:
	case rx::VF_F32x3:
	case rx::VF_F32x4:
		memcpy(out, src, components * sizeof(float32));
		break;
	case rx::VF_F16x2:
	case rx::VF_F16x4:
		for (uint32 i = 0; i < components; i++) {
			uint16 half;
			memcpy(&half, src + i * 2, sizeof(half));
			out[i] = HalfToFloat(half);
		}
		break;
	case rx::VF_UNorm8x4:
		for (uint32 i = 0; i < components; i++) {
			out[i] = static_cast<float32>(src[i]) / 255.0f;
		}
		break;
	case rx::VF_SNorm8x4:
		for (uint32 i = 0; i < components; i++) {
			out[i] = std::max(static_cast<float32>(static_cast<int8>(src[i])) / 127.0f, -1.0f);
		}
		break;
	case rx::VF_UInt8x4:
		for (uint32 i = 0; i < components; i++) {
			out[i] = static_cast<float32>(src[i]);
		}
		break;
	case rx::VF_UNorm16x2:
	case rx::VF_UNorm16x4:
		for (uint32 i = 0; i < components; i++) {
			uint16 value;
			memcpy(&value, src + i * 2, sizeof(value));
			out[i] = static_cast<float32>(value) / 65535.0f;
		}
		break;
	case rx::VF_SNorm16x4:
		for (uint32 i = 0; i < components; i++) {
			int16 value;
			memcpy(&value, src + i * 2, sizeof(value));
			out[i] = std::max(static_cast<float32>(value) / 32767.0f, -1.0f);
		}
		break;
	case rx::VF_UInt16x4:
		for (uint32 i = 0; i < components; i++) {
			uint16 value;
			memcpy(&value, src + i * 2, sizeof(value));
			out[i] = static_cast<float32>(value);
		}
		break;
	default:
		break;
	}
}

void ReadJoints(const AttributeView& view, uint32 vertex, uint32* out)
{
	const uint8* src = view.pData + static_cast<size_t>(vertex) * view.Stride;

	for (uint32 i = 0; i < scInfluencesPerVertex; i++) {
		if (view.Format == rx::VF_UInt8x4) {
			out[i] = src[i];
		}
		else {
			uint16 value;
			memcpy(&value, src + i * 2, sizeof(value));
			out[i] = value;
		}
	}
}

bool IsFormatAllowed(uint8 semantic, uint8 format)
{
	switch (semantic) {
	case rx::VS_Position:
		return format == rx::VF_F32x3;
	case rx::VS_Normal:
		return format == rx::VF_F32x3 || format == rx::VF_SNorm16x4 || format == rx::VF_SNorm8x4;
	case rx::VS_Tangent:
		return format == rx::VF_F32x4 || format == rx::VF_SNorm16x4 || format == rx::VF_SNorm8x4;
	case rx::VS_TexCoord:
		return format == rx::VF_F32x2 || format == rx::VF_F16x2 || format == rx::VF_UNorm16x2;
	case rx::VS_Color:
		return format == rx::VF_UNorm8x4 || format == rx::VF_F16x4 || format == rx::VF_F32x4;
	case rx::VS_Joints:
		return format == rx::VF_UInt8x4 || format == rx::VF_UInt16x4;
	case rx::VS_Weights:
		return format == rx::VF_UNorm8x4 || format == rx::VF_UNorm16x4 || format == rx::VF_F32x4;
	default:
		return rx::GetFormatSize(format) != 0;
	}
}

void ReadAttribute(const AttributeView& view, uint32 base_vertex, uint32 vertex_count, uint32 components,
				   SizedArray<float32>& out)
{
	if (!view.IsValid()) {
		return;
	}

	out.InitSize(static_cast<size_t>(vertex_count) * components);

	for (uint32 i = 0; i < vertex_count; i++) {
		float32 values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		ReadFloats(view, base_vertex + i, values);

		memcpy(&out[static_cast<size_t>(i) * components], values, components * sizeof(float32));
	}
}

void ReadInfluences(const MeshSource& source, uint32 base_vertex, uint32 vertex_count, SizedArray<uint32>& out_ids,
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
		Candidate candidates[scMaxWeightSets * scInfluencesPerVertex];
		uint32 candidate_count = 0;

		for (uint32 set = 0; set < scMaxWeightSets; set++) {
			if (!source.Joints[set].IsValid() || !source.Weights[set].IsValid()) {
				continue;
			}

			uint32 joints[scInfluencesPerVertex];
			float32 weights[scInfluencesPerVertex];

			ReadJoints(source.Joints[set], base_vertex + vertex, joints);
			ReadFloats(source.Weights[set], base_vertex + vertex, weights);

			for (uint32 k = 0; k < scInfluencesPerVertex; k++) {
				if (!(weights[k] > 0.0f) || joints[k] >= source.JointCount) {
					continue;
				}

				candidates[candidate_count++] = { joints[k], weights[k] };
			}
		}

		const uint32 kept = std::min(candidate_count, scInfluencesPerVertex);

		std::partial_sort(candidates, candidates + kept, candidates + candidate_count,
						  [](const Candidate& a, const Candidate& b) { return a.Weight > b.Weight; });

		float32 sum = 0.0f;

		for (uint32 k = 0; k < kept; k++) {
			sum += candidates[k].Weight;
		}

		for (uint32 k = 0; k < scInfluencesPerVertex; k++) {
			const size_t slot = static_cast<size_t>(vertex) * scInfluencesPerVertex + k;

			if (sum > 1e-6f && k < kept) {
				out_ids[slot] = candidates[k].Joint + source.JointOffset;
				out_weights[slot] = candidates[k].Weight / sum;
			}
			else {
				out_ids[slot] = source.JointOffset;
				out_weights[slot] = (sum <= 1e-6f && k == 0) ? 1.0f : 0.0f;
			}
		}
	}
}

void TransformGeometry(SizedArray<float32>& positions, SizedArray<float32>& normals, SizedArray<float32>& tangents,
					   SizedArray<uint32>& indices, const Mat4f& matrix)
{
	const float32* m = matrix.RawData;

	const float32 a00 = m[0], a01 = m[1], a02 = m[2];
	const float32 a10 = m[4], a11 = m[5], a12 = m[6];
	const float32 a20 = m[8], a21 = m[9], a22 = m[10];

	const float32 c00 = a11 * a22 - a12 * a21;
	const float32 c01 = a12 * a20 - a10 * a22;
	const float32 c02 = a10 * a21 - a11 * a20;
	const float32 c10 = a02 * a21 - a01 * a22;
	const float32 c11 = a00 * a22 - a02 * a20;
	const float32 c12 = a01 * a20 - a00 * a21;
	const float32 c20 = a01 * a12 - a02 * a11;
	const float32 c21 = a02 * a10 - a00 * a12;
	const float32 c22 = a00 * a11 - a01 * a10;

	const float32 determinant = a00 * c00 + a01 * c01 + a02 * c02;
	const float32 sign = (determinant < 0.0f) ? -1.0f : 1.0f;

	for (size_t i = 0; i + 2 < positions.Size; i += 3) {
		float32* p = &positions[i];

		const float32 x = p[0];
		const float32 y = p[1];
		const float32 z = p[2];

		p[0] = x * a00 + y * a10 + z * a20 + m[12];
		p[1] = x * a01 + y * a11 + z * a21 + m[13];
		p[2] = x * a02 + y * a12 + z * a22 + m[14];
	}

	for (size_t i = 0; i + 2 < normals.Size; i += 3) {
		float32* n = &normals[i];

		float32 nx = (n[0] * c00 + n[1] * c10 + n[2] * c20) * sign;
		float32 ny = (n[0] * c01 + n[1] * c11 + n[2] * c21) * sign;
		float32 nz = (n[0] * c02 + n[1] * c12 + n[2] * c22) * sign;

		const float32 length = std::sqrt(nx * nx + ny * ny + nz * nz);

		if (length > 1e-20f) {
			nx /= length;
			ny /= length;
			nz /= length;
		}

		n[0] = nx;
		n[1] = ny;
		n[2] = nz;
	}

	for (size_t i = 0; i + 3 < tangents.Size; i += 4) {
		float32* t = &tangents[i];

		float32 tx = t[0] * a00 + t[1] * a10 + t[2] * a20;
		float32 ty = t[0] * a01 + t[1] * a11 + t[2] * a21;
		float32 tz = t[0] * a02 + t[1] * a12 + t[2] * a22;

		const float32 length = std::sqrt(tx * tx + ty * ty + tz * tz);

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

bool BuildSubmeshMesh(PrimitiveMesh& mesh, const MeshSource& source, const rx::Submesh& submesh, bool has_normal_map,
					  const Mat4f* bake_transform)
{
	const uint32 vertex_count = submesh.VertexCount;

	SizedArray<uint32> indices;
	indices.InitSize(submesh.IndexCount);

	for (uint32 i = 0; i < submesh.IndexCount; i++) {
		const uint8* src = source.pIndices + static_cast<size_t>(submesh.FirstIndex + i) * source.IndexSize;

		uint32 index = 0;

		if (source.IndexSize == 2) {
			uint16 value;
			memcpy(&value, src, sizeof(value));
			index = value;
		}
		else {
			memcpy(&index, src, sizeof(index));
		}

		if (index >= vertex_count) {
			return false;
		}

		indices[i] = index;
	}

	SizedArray<float32> positions;
	SizedArray<float32> normals;
	SizedArray<float32> uvs;
	SizedArray<float32> tangents;
	SizedArray<float32> weights;
	SizedArray<uint32> bone_ids;

	ReadAttribute(source.Position, submesh.BaseVertex, vertex_count, 3, positions);
	ReadAttribute(source.Normal, submesh.BaseVertex, vertex_count, 3, normals);
	ReadAttribute(source.TexCoord, submesh.BaseVertex, vertex_count, 2, uvs);
	ReadAttribute(source.Tangent, submesh.BaseVertex, vertex_count, 4, tangents);

	if ((submesh.Flags & rx::scSubmeshSkinned) != 0) {
		ReadInfluences(source, submesh.BaseVertex, vertex_count, bone_ids, weights);
	}

	if (normals.IsEmpty()) {
		MeshUtil::ExpandByIndices(positions, 3, indices);
		MeshUtil::ExpandByIndices(uvs, 2, indices);
		MeshUtil::ExpandByIndices(tangents, 4, indices);
		MeshUtil::ExpandByIndices(weights, scInfluencesPerVertex, indices);
		MeshUtil::ExpandByIndices(bone_ids, scInfluencesPerVertex, indices);

		for (uint32 i = 0; i < indices.Size; i++) {
			indices[i] = i;
		}

		MeshUtil::GenerateFlatNormals(positions, normals);
	}

	if (tangents.IsEmpty() && !uvs.IsEmpty() && has_normal_map) {
		MeshUtil::GenerateTangents(positions, normals, uvs, indices, tangents);

		for (size_t i = 3; i < tangents.Size; i += 4) {
			tangents[i] = -tangents[i];
		}
	}

	if (bake_transform != nullptr) {
		TransformGeometry(positions, normals, tangents, indices, *bake_transform);
	}

	mesh.VertexList.CreateFrom(positions, normals, uvs, tangents, weights, bone_ids, eVertexCreateFlags::DefaultLayout);
	mesh.SetIndices(std::move(indices));

	return true;
}

bool IsFileKtx2(const String& path)
{
	std::FILE* fp = std::fopen(path.CStr(), "rb");

	if (fp == nullptr) {
		return false;
	}

	uint8 header[sizeof(scKtx2Magic)] {};
	const size_t header_size = std::fread(header, 1, sizeof(header), fp);
	std::fclose(fp);

	return header_size == sizeof(header) && memcmp(header, scKtx2Magic, sizeof(scKtx2Magic)) == 0;
}

float32 GetParam(const RxMaterialDesc& desc, const StringView name, float32 fallback)
{
	const RxMaterialParam* param = desc.FindParam(name);

	return (param != nullptr && param->Values.Size > 0) ? param->Values[0] : fallback;
}

} // namespace

String LoaderRxModel::ResolvePath(const String& relative_path) const { return mDirectory + relative_path; }

bool LoaderRxModel::ShouldWarn(const String& key) { return mWarned.insert(key).second; }

bool LoaderRxModel::CheckFileHash(const RxBinaryFile& file, const RxFileRef& file_ref) const
{
	if (!file_ref.bHasHash || file.GetHeader().ContentHash == file_ref.Hash) {
		return true;
	}

	LogError(LC_ASSET, "{}: '{}' is stale, its content hash is {:016x} but the config expects {:016x}", mModelPath,
			 file.GetPath(), file.GetHeader().ContentHash, file_ref.Hash);

	return false;
}

int32 LoaderRxModel::FindJoint(const char* name) const
{
	if (!mpSkeleton) {
		return -1;
	}

	for (uint32 i = 0; i < mpSkeleton->JointCount; i++) {
		if (strcmp(mpSkeleton->BoneNames[i].CStr(), name) == 0) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

bool LoaderRxModel::ReadSkeleton(const RxFileRef& file_ref, Ref<Skeleton>& out_skeleton, uint64& out_signature) const
{
	RxBinaryFile file;

	if (!file.Open(ResolvePath(file_ref.Path), "SKEL") || !CheckFileHash(file, file_ref)) {
		return false;
	}

	uint32 joint_count = 0;
	uint32 bind_count = 0;
	uint32 inverse_bind_count = 0;

	const rx::SkeletonHeader* header = file.GetSingle<rx::SkeletonHeader>("SHDR");
	const rx::Joint* joints = file.GetChunk<rx::Joint>("JNTS", joint_count);
	const rx::Transform* bind_pose = file.GetChunk<rx::Transform>("BPOS", bind_count);
	const rx::Matrix4* inverse_bind = file.GetChunk<rx::Matrix4>("INVB", inverse_bind_count);

	if (header == nullptr || joints == nullptr || bind_pose == nullptr || inverse_bind == nullptr) {
		return false;
	}

	if (header->JointCount == 0 || joint_count != header->JointCount || bind_count != header->JointCount ||
		inverse_bind_count != header->JointCount) {
		LogError(LC_ASSET, "'{}' has chunks that do not match its joint count ({})", file.GetPath(),
				 header->JointCount);
		return false;
	}

	const Mat4f root_transform = Mat4f::AsScale(mDesc.Root.Scale) * Mat4f::AsRotation(mDesc.Root.Rotation) *
								 Mat4f::AsTranslation(mDesc.Root.Position);

	Ref<Skeleton> skeleton = Ref<Skeleton>::New();
	Skeleton& skel = *skeleton;

	skel.JointCount = joint_count;

	skel.InvBindTransforms.InitSize(joint_count);
	skel.ParentIndices.InitSize(joint_count);
	skel.BoneNames.InitSize(joint_count);
	skel.RestPose.InitSize(joint_count);
	skel.RootTransforms.InitSize(joint_count);
	skel.LocalTransforms.InitSize(joint_count);
	skel.WorldTransforms.InitSize(joint_count);
	skel.SkinningMatrices.InitSize(joint_count);

	for (uint32 i = 0; i < joint_count; i++) {
		const rx::Joint& joint = joints[i];

		if (joint.Parent < -1 || joint.Parent >= static_cast<int32>(i)) {
			LogError(LC_ASSET, "'{}' joint {} has parent {}, joints must be stored parent-first", file.GetPath(), i,
					 joint.Parent);
			return false;
		}

		const char* name = file.GetString(joint.Name);

		skel.BoneNames[i] = (name[0] != '\0') ? String(name) : String::Fmt("joint_{}", i);
		skel.ParentIndices[i] = (joint.Parent < 0) ? BoneNull : static_cast<uint32>(joint.Parent);

		BoneRestPose rest {};
		rest.Translation = Vec3f(bind_pose[i].Translation[0], bind_pose[i].Translation[1], bind_pose[i].Translation[2]);
		rest.Rotation = Quat(bind_pose[i].Rotation[0], bind_pose[i].Rotation[1], bind_pose[i].Rotation[2],
							 bind_pose[i].Rotation[3]);
		rest.Scale = Vec3f(bind_pose[i].Scale[0], bind_pose[i].Scale[1], bind_pose[i].Scale[2]);

		skel.RestPose[i] = rest;

		float32 matrix[16];
		memcpy(matrix, inverse_bind[i].M, sizeof(matrix));

		skel.InvBindTransforms[i] = Mat4f(matrix);
		skel.RootTransforms[i] = (joint.Parent < 0) ? root_transform : Mat4f::scIdentity;
	}

	skel.BuildEvaluationOrder();

	out_skeleton = skeleton;
	out_signature = header->Signature;

	return true;
}

bool LoaderRxModel::LoadSkeleton()
{
	if (!ReadSkeleton(mDesc.Skeleton, mpSkeleton, mSkeletonSignature)) {
		return false;
	}

	mJointCount = mpSkeleton->JointCount;

	return true;
}

bool LoaderRxModel::LinkPartSkeleton()
{
	Ref<Skeleton> part { nullptr };

	if (!ReadSkeleton(mDesc.Skeleton, part, mSkeletonSignature)) {
		return false;
	}

	if (part->JointCount != mJointCount) {
		LogError(LC_ASSET, "{}: '{}' has {} joints, the weapon rig expects {}", mModelPath,
				 ResolvePath(mDesc.Skeleton.Path), part->JointCount, mJointCount);
		return false;
	}

	Skeleton& rig = *mpSkeleton;

	for (uint32 i = 0; i < mJointCount; i++) {
		const uint32 rig_joint = mJointOffset + i;

		if (!(rig.BoneNames[rig_joint] == part->BoneNames[i])) {
			LogError(LC_ASSET, "{}: joint {} is '{}' but the weapon rig has '{}' there, re-export the weapon",
					 mModelPath, i, part->BoneNames[i], rig.BoneNames[rig_joint]);
			return false;
		}

		rig.InvBindTransforms[rig_joint] = part->InvBindTransforms[i];
	}

	return true;
}

bool LoaderRxModel::ValidateTextures() const
{
	bool is_valid = true;

	for (const RxMaterialDesc& material : mDesc.Materials) {
		for (const auto& [slot, path] : material.Textures) {
			if (!IsFileKtx2(ResolvePath(path))) {
				LogError(LC_ASSET, "{}: texture '{}' of material '{}' is missing or is not a KTX 2.0 file ('{}')",
						 mModelPath, slot, material.Name, ResolvePath(path));
				is_valid = false;
			}
		}
	}

	return is_valid;
}

bool LoaderRxModel::LoadMaterialTexture(const RxMaterialDesc& desc, const char* slot, MaterialComponent& component,
										bool* out_has_alpha)
{
	const String* relative_path = desc.FindTexture(MakeStringView(slot));

	if (relative_path == nullptr) {
		return false;
	}

	const String path = ResolvePath(*relative_path);

	loader::LoaderKtx ktx {};

	if (!IsFileKtx2(path) || !ktx.Open(path.CStr()) || !ktx.IsOpen()) {
		LogError(LC_ASSET, "Could not load KTX2 texture '{}' ({} of material '{}' in '{}')", path, slot, desc.Name,
				 mModelPath);
		return false;
	}

	ImageInfo image_info = ktx.MakeImageInfo();

	if (image_info.ImageData.pData == nullptr) {
		return false;
	}

	if (out_has_alpha != nullptr) {
		*out_has_alpha = ktx.HasTranslucentTexels();
	}

	component.UploadSrc = eMaterialComponentUploadSrc::DirectUpload;
	component.ImageToUpload = image_info;

	return true;
}

MaterialID LoaderRxModel::MakeMaterial(const String& name, bool is_skinned)
{
	const auto cache_key = std::make_pair(name, is_skinned);

	auto cached = mMaterialCache.find(cache_key);

	if (cached != mMaterialCache.end()) {
		return cached->second;
	}

	const RxMaterialDesc* desc = mDesc.FindMaterial(name.GetView());

	if (desc == nullptr) {
		return MaterialID::scNull;
	}

	MaterialID material_id = gMaterialManager->TryNewMaterial(String(name), is_skinned);

	if (material_id.IsNull()) {
		return material_id;
	}

	Material* material = gMaterialManager->GetMaterial(material_id);

	mMaterialCache[cache_key] = material_id;

	static constexpr StringView known_slots[] = { "albedo", "normal", "orm", "emissive" };
	static constexpr StringView known_params[] = { "base_color",		 "alpha",		   "roughness",
												   "metallic",			 "emissive",	   "emissive_strength",
												   "occlusion_strength", "normal_strength" };

	for (const auto& [slot, path] : desc->Textures) {
		if (std::find(std::begin(known_slots), std::end(known_slots), slot) == std::end(known_slots) &&
			ShouldWarn(String::Fmt("slot:{}", slot))) {
			LogWarning(LC_ASSET, "'{}' uses texture slot '{}', which is not supported and is ignored", mModelPath,
					   slot);
		}
	}

	for (const RxMaterialParam& param : desc->Params) {
		if (std::find(std::begin(known_params), std::end(known_params), param.Name) == std::end(known_params) &&
			ShouldWarn(String::Fmt("param:{}", param.Name))) {
			LogWarning(LC_ASSET, "'{}' uses material param '{}', which is not supported and is ignored", mModelPath,
					   param.Name);
		}
	}

	if (std::abs(GetParam(*desc, "normal_strength", 1.0f) - 1.0f) > 1e-4f && ShouldWarn("normal_strength")) {
		LogWarning(LC_ASSET, "'{}' sets normal_strength, which is not supported and is ignored", mModelPath);
	}

	bool diffuse_has_alpha = false;
	bool* out_diffuse_has_alpha = (desc->Blend == eRxBlendMode::Blend) ? &diffuse_has_alpha : nullptr;

	if (!LoadMaterialTexture(*desc, "albedo", material->Diffuse, out_diffuse_has_alpha)) {
		material->Diffuse.SetTicket(gAssetManager->GetNullImageTicket(eImageFormat::RGBA8_UNorm));
	}

	float32 alpha = 1.0f;

	if (const RxMaterialParam* base_color = desc->FindParam("base_color")) {
		if (base_color->Values.Size >= 3) {
			material->SetBaseColorFactor(base_color->Values.pData);
		}

		if (base_color->Values.Size >= 4) {
			alpha = base_color->Values[3];
		}
	}

	alpha *= GetParam(*desc, "alpha", 1.0f);

	LoadMaterialTexture(*desc, "normal", material->NormalMap);

	const bool has_orm = LoadMaterialTexture(*desc, "orm", material->MetallicRoughness);

	material->SetMetallicRoughness(GetParam(*desc, "metallic", has_orm ? 1.0f : 0.0f),
								   GetParam(*desc, "roughness", has_orm ? 1.0f : 0.5f));
	material->SetOcclusionStrength(has_orm ? GetParam(*desc, "occlusion_strength", 1.0f) : 0.0f);

	const bool has_emissive = LoadMaterialTexture(*desc, "emissive", material->Emissive);

	float32 emissive_color[3] = { has_emissive ? 1.0f : 0.0f, has_emissive ? 1.0f : 0.0f, has_emissive ? 1.0f : 0.0f };

	if (const RxMaterialParam* emissive = desc->FindParam("emissive"); emissive && emissive->Values.Size >= 3) {
		memcpy(emissive_color, emissive->Values.pData, sizeof(emissive_color));
	}

	material->SetEmissive(emissive_color, GetParam(*desc, "emissive_strength", 1.0f));

	if (desc->Blend == eRxBlendMode::Blend) {
		if (alpha < 0.99f) {
			material->SetAlpha(alpha);
		}
		else if (diffuse_has_alpha) {
			material->SetAlpha(0.99f);
		}
		else {
			material->SetAlpha(1.0f);
		}
	}
	else if (desc->Blend == eRxBlendMode::Masked) {
		material->SetAlphaMask(true);
		material->SetAlphaCutoff(desc->AlphaCutoff);
		material->SetAlpha(alpha);
	}
	else {
		material->SetAlpha(1.0f);
	}

	if (desc->bDoubleSided && desc->Blend != eRxBlendMode::Opaque) {
		material->SetDoubleSided(true);
	}

	if (desc->Shader == "unlit") {
		material->SetUnlit(true);
	}

	material->Finalize();

	return material_id;
}

bool LoaderRxModel::BuildObjects(const RxBinaryFile& mesh_file)
{
	const String& mesh_path = mesh_file.GetPath();

	MeshSource source;

	uint32 attribute_count = 0;
	uint32 submesh_count = 0;

	source.pHeader = mesh_file.GetSingle<rx::MeshHeader>("MHDR");

	const rx::VertexAttribute* attributes = mesh_file.GetChunk<rx::VertexAttribute>("VATR", attribute_count);
	source.pSubmeshes = mesh_file.GetChunk<rx::Submesh>("SUBM", submesh_count);

	const rx::ChunkEntry* index_chunk = mesh_file.FindChunk("IDXB");

	if (source.pHeader == nullptr || attributes == nullptr || source.pSubmeshes == nullptr) {
		return false;
	}

	const rx::MeshHeader& header = *source.pHeader;

	if (index_chunk == nullptr || (header.IndexSize != 2 && header.IndexSize != 4) ||
		index_chunk->Stride != header.IndexSize || index_chunk->Count != header.IndexCount) {
		LogError(LC_ASSET, "'{}' has a missing or malformed index buffer", mesh_path);
		return false;
	}

	source.pIndices = mesh_file.GetChunkData(*index_chunk);
	source.IndexSize = header.IndexSize;

	if (attribute_count != header.AttributeCount || submesh_count != header.SubmeshCount ||
		header.StreamCount > rx::scMaxStreams || header.VertexCount == 0) {
		LogError(LC_ASSET, "'{}' has chunks that do not match its mesh header", mesh_path);
		return false;
	}

	const rx::ChunkEntry* streams[rx::scMaxStreams] = {};

	for (uint32 i = 0; i < header.StreamCount; i++) {
		const std::string id = std::format("VB{:02}", i);

		streams[i] = mesh_file.FindChunk(id.c_str());

		if (streams[i] == nullptr || streams[i]->Count != header.VertexCount) {
			LogError(LC_ASSET, "'{}' has a missing or malformed vertex stream '{}'", mesh_path, id);
			return false;
		}
	}

	uint32 position_count = 0;

	for (uint32 i = 0; i < attribute_count; i++) {
		const rx::VertexAttribute& attribute = attributes[i];
		const uint32 format_size = rx::GetFormatSize(attribute.Format);

		if (attribute.Stream >= header.StreamCount || format_size == 0 ||
			attribute.Offset + format_size > streams[attribute.Stream]->Stride ||
			!IsFormatAllowed(attribute.Semantic, attribute.Format)) {
			LogError(LC_ASSET, "'{}' vertex attribute {} (semantic {}, format {}) is malformed", mesh_path, i,
					 attribute.Semantic, attribute.Format);
			return false;
		}

		AttributeView view;
		view.pData = mesh_file.GetChunkData(*streams[attribute.Stream]) + attribute.Offset;
		view.Stride = streams[attribute.Stream]->Stride;
		view.Format = attribute.Format;

		switch (attribute.Semantic) {
		case rx::VS_Position:
			source.Position = view;
			position_count++;
			break;
		case rx::VS_Normal:
			source.Normal = view;
			break;
		case rx::VS_Tangent:
			source.Tangent = view;
			break;
		case rx::VS_TexCoord:
			if (attribute.SemanticIndex == 0) {
				source.TexCoord = view;
			}
			break;
		case rx::VS_Joints:
			if (attribute.SemanticIndex < scMaxWeightSets) {
				source.Joints[attribute.SemanticIndex] = view;
			}
			break;
		case rx::VS_Weights:
			if (attribute.SemanticIndex < scMaxWeightSets) {
				source.Weights[attribute.SemanticIndex] = view;
			}
			break;
		case rx::VS_Color:
			if (ShouldWarn("vertex_colors")) {
				LogWarning(LC_ASSET, "'{}' has vertex colors, which are not supported and are ignored", mesh_path);
			}
			break;
		default:
			break;
		}
	}

	if (position_count != 1) {
		LogError(LC_ASSET, "'{}' must have exactly one POSITION attribute, found {}", mesh_path, position_count);
		return false;
	}

	bool needs_skeleton = false;

	for (uint32 i = 0; i < submesh_count; i++) {
		const rx::Submesh& submesh = source.pSubmeshes[i];

		needs_skeleton |= (submesh.Flags & rx::scSubmeshSkinned) != 0 || submesh.ParentJoint != rx::scNoJoint;
	}

	if (mbIsPart && !mDesc.bHasSkeleton) {
		LogError(LC_ASSET, "{}: a model used by a .rxweapon needs a model.skeleton", mModelPath);
		return false;
	}

	if (needs_skeleton && !mDesc.bHasSkeleton) {
		LogError(LC_ASSET, "{}: '{}' has skinned or joint-attached submeshes but the config has no model.skeleton",
				 mModelPath, mesh_path);
		return false;
	}

	if (mDesc.bHasSkeleton) {
		if (!(mbIsPart ? LinkPartSkeleton() : LoadSkeleton())) {
			return false;
		}

		if ((needs_skeleton || header.SkeletonSignature != 0) && header.SkeletonSignature != mSkeletonSignature) {
			LogError(LC_ASSET, "{}: '{}' was built against a different skeleton than '{}' ({:016x} != {:016x})",
					 mModelPath, mesh_path, ResolvePath(mDesc.Skeleton.Path), header.SkeletonSignature,
					 mSkeletonSignature);
			return false;
		}

		source.JointCount = mJointCount;
		source.JointOffset = mJointOffset;
	}

	std::vector<const rx::Submesh*> drawn_submeshes;

	for (uint32 i = 0; i < submesh_count; i++) {
		const rx::Submesh& submesh = source.pSubmeshes[i];
		const bool is_skinned = (submesh.Flags & rx::scSubmeshSkinned) != 0;

		const bool has_valid_ranges = submesh.IndexCount > 0 && (submesh.IndexCount % 3) == 0 &&
									  submesh.FirstIndex <= header.IndexCount &&
									  submesh.IndexCount <= header.IndexCount - submesh.FirstIndex &&
									  submesh.VertexCount > 0 && submesh.BaseVertex <= header.VertexCount &&
									  submesh.VertexCount <= header.VertexCount - submesh.BaseVertex &&
									  submesh.Lod < std::max(header.LodCount, 1u);

		if (!has_valid_ranges) {
			LogError(LC_ASSET, "'{}' submesh {} has index, vertex or LOD ranges outside of the mesh", mesh_path, i);
			return false;
		}

		if (is_skinned && (!source.Joints[0].IsValid() || !source.Weights[0].IsValid())) {
			LogError(LC_ASSET, "'{}' submesh {} is skinned but the mesh has no JOINTS and WEIGHTS", mesh_path, i);
			return false;
		}

		if (!is_skinned && submesh.ParentJoint != rx::scNoJoint && submesh.ParentJoint >= source.JointCount) {
			LogError(LC_ASSET, "'{}' submesh {} is attached to joint {}, the skeleton has {}", mesh_path, i,
					 submesh.ParentJoint, source.JointCount);
			return false;
		}

		const char* material_name = mesh_file.GetString(submesh.Material);

		if (mDesc.FindMaterial(MakeStringView(material_name)) == nullptr) {
			LogError(LC_ASSET, "{}: submesh {} of '{}' uses material '{}', which has no entry under materials",
					 mModelPath, i, mesh_path, material_name);
			return false;
		}

		if (submesh.Lod == 0) {
			drawn_submeshes.push_back(&submesh);
		}
	}

	if (drawn_submeshes.empty()) {
		LogError(LC_ASSET, "'{}' has no submeshes in LOD 0", mesh_path);
		return false;
	}

	if (header.LodCount > 1 && ShouldWarn("lods")) {
		LogInfo(LC_ASSET, "'{}' has {} LODs, only LOD 0 is loaded", mesh_path, header.LodCount);
	}

	for (const RxSocketDesc& socket : mDesc.Sockets) {
		if (!socket.Joint.IsEmpty() && FindJoint(socket.Joint.CStr()) < 0) {
			LogError(LC_ASSET, "{}: socket '{}' is attached to joint '{}', which is not in the skeleton", mModelPath,
					 socket.Name, socket.Joint);
			return false;
		}
	}

	for (const RxClipDesc& clip : mDesc.Clips) {
		if (!clip.RootJoint.IsEmpty() && FindJoint(clip.RootJoint.CStr()) < 0) {
			LogError(LC_ASSET, "{}: animation '{}' has root_joint '{}', which is not in the skeleton", mModelPath,
					 clip.Name, clip.RootJoint);
			return false;
		}
	}

	if (!ValidateTextures()) {
		return false;
	}

	const Mat4f root_transform = Mat4f::AsScale(mDesc.Root.Scale) * Mat4f::AsRotation(mDesc.Root.Rotation) *
								 Mat4f::AsTranslation(mDesc.Root.Position);

	std::vector<BuiltSubmesh> built;
	built.reserve(drawn_submeshes.size());

	for (const rx::Submesh* submesh : drawn_submeshes) {
		const bool is_skinned = (submesh->Flags & rx::scSubmeshSkinned) != 0;
		const bool is_static = !is_skinned && submesh->ParentJoint == rx::scNoJoint && mStaticJoint < 0;

		const RxMaterialDesc* material = mDesc.FindMaterial(MakeStringView(mesh_file.GetString(submesh->Material)));

		BuiltSubmesh& entry = built.emplace_back();
		entry.pSubmesh = submesh;
		entry.pMesh = Ref<PrimitiveMesh>::New();
		entry.pMesh->bKeepInMemory = bKeepInMemory;

		const Mat4f* bake_transform = (is_static && !mDesc.Root.bIsIdentity) ? &root_transform : nullptr;

		if (!BuildSubmeshMesh(*entry.pMesh, source, *submesh, material->FindTexture("normal") != nullptr,
							  bake_transform)) {
			LogError(LC_ASSET, "'{}' submesh '{}' has indices outside of its vertex range", mesh_path,
					 mesh_file.GetString(submesh->Name));
			return false;
		}
	}

	Object* output_object = mpRootObject;

	const bool has_multiple_submeshes = built.size() > 1 || mbIsPart;

	int32 attach_id = 1;

	bool has_root_bounds = false;
	BBox root_bounds;

	for (BuiltSubmesh& entry : built) {
		const rx::Submesh& submesh = *entry.pSubmesh;
		const bool is_skinned = (submesh.Flags & rx::scSubmeshSkinned) != 0;

		Object* current_object = output_object;

		if (has_multiple_submeshes) {
			const char* submesh_name = mesh_file.GetString(submesh.Name);

			current_object = gObjectManager->NewObject((submesh_name[0] != '\0')
														   ? std::string(submesh_name)
														   : std::format("{}_{}", output_object->Name.Get(), attach_id),
													   MaterialID::scNull, eObjectTag::None, true);

			attach_id++;
		}

		current_object->pMesh = entry.pMesh;
		current_object->Bounds = MeshUtil::CalculateBounds(entry.pMesh->VertexList);

		const MaterialID material_id = MakeMaterial(mesh_file.GetString(submesh.Material), is_skinned);

		if (!material_id.IsNull()) {
			current_object->SetMaterial(material_id);
		}

		if (is_skinned) {
			current_object->pSkeleton = mpSkeleton;
		}
		else if (submesh.ParentJoint != rx::scNoJoint || mStaticJoint >= 0) {
			const uint32 joint = (submesh.ParentJoint != rx::scNoJoint) ? mJointOffset + submesh.ParentJoint
																		 : static_cast<uint32>(mStaticJoint);

			current_object->AttachToBone(mpSkeleton, joint, mpSkeleton->InvBindTransforms[joint]);
		}

		if (has_multiple_submeshes) {
			AttachChild(output_object, current_object->ID);

			if (has_root_bounds) {
				root_bounds.Min = Vec3f::Min(root_bounds.Min, current_object->Bounds.Min);
				root_bounds.Max = Vec3f::Max(root_bounds.Max, current_object->Bounds.Max);
			}
			else {
				root_bounds = current_object->Bounds;
				has_root_bounds = true;
			}
		}
	}

	if (has_root_bounds && !mbIsPart) {
		output_object->Bounds = root_bounds;
	}

	if (!mbIsPart) {
		LoadClips(mDesc.Clips);
	}

	return true;
}

bool LoaderRxModel::LoadClip(Animation& out_anim, const RxClipDesc& clip)
{
	RxBinaryFile file;

	if (!file.Open(ResolvePath(clip.File.Path), "ANIM") || !CheckFileHash(file, clip.File)) {
		return false;
	}

	uint32 track_count = 0;

	const rx::AnimHeader* header = file.GetSingle<rx::AnimHeader>("AHDR");
	const rx::Track* tracks = file.GetChunk<rx::Track>("TRKS", track_count);

	if (header == nullptr || tracks == nullptr) {
		return false;
	}

	if (track_count != header->TrackCount || header->FrameCount == 0 ||
		(header->FrameCount > 1 && !(header->SampleRate > 0.0f))) {
		LogError(LC_ASSET, "'{}' has a malformed animation header", file.GetPath());
		return false;
	}

	if ((header->Flags & rx::scAnimAdditive) != 0) {
		LogWarning(LC_ASSET, "'{}' is an additive clip, which is not supported, skipping animation '{}'",
				   file.GetPath(), clip.Name);
		return false;
	}

	const rx::ChunkEntry* constants = file.FindChunk("CNST");
	const rx::ChunkEntry* frames = file.FindChunk("FRMS");

	if (frames != nullptr && frames->Count != header->FrameCount) {
		LogError(LC_ASSET, "'{}' has {} frame blocks, its header expects {}", file.GetPath(), frames->Count,
				 header->FrameCount);
		return false;
	}

	const uint32 joint_count = mpSkeleton->JointCount;
	const bool bind_by_index = (header->SkeletonSignature == mSkeletonSignature);

	out_anim.Name = String(clip.Name);
	out_anim.Duration = (header->FrameCount > 1) ? static_cast<float32>(header->FrameCount - 1) / header->SampleRate
												 : 0.0f;
	out_anim.BoneTracks.InitSize(joint_count);

	for (uint32 i = 0; i < joint_count; i++) {
		out_anim.BoneTracks.pData[i] = BoneTrack {};
	}

	uint32 dropped_tracks = 0;

	for (uint32 track_index = 0; track_index < track_count; track_index++) {
		const rx::Track& track = tracks[track_index];

		if (track.Channel == rx::TC_Curve) {
			if (ShouldWarn("curve_tracks")) {
				LogWarning(LC_ASSET, "'{}' has curve tracks, which are not supported and are ignored", file.GetPath());
			}
			continue;
		}

		if (track.Channel > rx::TC_Scale || track.Format != rx::scTrackFormatF32) {
			if (ShouldWarn("track_format")) {
				LogWarning(LC_ASSET, "'{}' has tracks with an unknown channel or format, they are ignored",
						   file.GetPath());
			}
			continue;
		}

		const int32 joint = bind_by_index ? static_cast<int32>(track.TargetIndex)
										  : FindJoint(file.GetString(track.TargetName));

		if (joint < 0 || static_cast<uint32>(joint) >= joint_count) {
			dropped_tracks++;
			continue;
		}

		const uint32 value_size = (track.Channel == rx::TC_Rotation) ? 16 : 12;
		const bool is_constant = (track.Flags & rx::scTrackConstant) != 0;

		const rx::ChunkEntry* data_chunk = is_constant ? constants : frames;
		const uint32 key_count = is_constant ? 1 : header->FrameCount;
		const uint64 block_size = (data_chunk == nullptr) ? 0 : (is_constant ? data_chunk->Size : data_chunk->Stride);

		if (track.KeyCount != key_count || static_cast<uint64>(track.DataOffset) + value_size > block_size) {
			LogError(LC_ASSET, "'{}' track {} points outside of its {} data", file.GetPath(), track_index,
					 is_constant ? "constant" : "frame");
			return false;
		}

		const uint8* data = file.GetChunkData(*data_chunk) + track.DataOffset;
		const size_t key_stride = is_constant ? 0 : data_chunk->Stride;

		SizedArray<float32> times;
		times.InitSize(key_count);

		for (uint32 key = 0; key < key_count; key++) {
			times[key] = is_constant ? 0.0f : static_cast<float32>(key) / header->SampleRate;
		}

		BoneTrack& bone_track = out_anim.BoneTracks.pData[joint];

		if (track.Channel == rx::TC_Rotation) {
			bone_track.Rotation.Times = std::move(times);
			bone_track.Rotation.Values.InitSize(key_count);

			for (uint32 key = 0; key < key_count; key++) {
				float32 value[4];
				memcpy(value, data + key * key_stride, sizeof(value));

				bone_track.Rotation.Values[key] = Quat(value[0], value[1], value[2], value[3]);
			}
		}
		else {
			BoneTransformTrack<Vec3f>& vector_track = (track.Channel == rx::TC_Translation) ? bone_track.Translation
																							: bone_track.Scale;

			vector_track.Times = std::move(times);
			vector_track.Values.InitSize(key_count);

			for (uint32 key = 0; key < key_count; key++) {
				float32 value[3];
				memcpy(value, data + key * key_stride, sizeof(value));

				vector_track.Values[key] = Vec3f(value[0], value[1], value[2]);
			}
		}
	}

	if (dropped_tracks > 0) {
		LogWarning(LC_ASSET, "Animation '{}' in '{}' has {} tracks for joints that are not in the skeleton", clip.Name,
				   mModelPath, dropped_tracks);
	}

	LogInfo(LC_ASSET, "Loaded animation '{}': {:.3f}s, {} joints", out_anim.Name, out_anim.Duration, joint_count);

	return true;
}

void LoaderRxModel::LoadClips(const DynArray<RxClipDesc>& clips)
{
	if (clips.Size == 0) {
		return;
	}

	if (!mpSkeleton) {
		LogWarning(LC_ASSET, "'{}' has animations but no skeleton, they are ignored", mModelPath);
		return;
	}

	Skeleton& skel = *mpSkeleton;

	skel.Animations.InitCapacity(clips.Size);

	int32 rest_index = -1;
	float32 rest_speed = 1.0f;

	for (const RxClipDesc& clip : clips) {
		Animation anim;

		if (!LoadClip(anim, clip)) {
			continue;
		}

		skel.Animations.Insert(std::move(anim));

		if (clip.bRootMotion && ShouldWarn("root_motion")) {
			LogWarning(LC_ASSET, "'{}' uses root motion, which is not supported, the motion plays on the root joint",
					   mModelPath);
		}

		if (rest_index < 0 && clip.bLoop) {
			rest_index = static_cast<int32>(skel.Animations.Size) - 1;
			rest_speed = clip.Speed;
		}
	}

	if (rest_index >= 0) {
		skel.SetRestAnimation(&skel.Animations[rest_index], rest_speed);
	}

	LogInfo(LC_ASSET, "Loaded {} animations", skel.Animations.Size);
}

void LoaderRxModel::SetModelPath(const String& path)
{
	mModelPath = path;

	const std::string model_path = path.CStr();
	const size_t slash_index = model_path.find_last_of('/');

	mDirectory = (slash_index != std::string::npos) ? model_path.substr(0, slash_index + 1) : std::string();
}

bool LoaderRxModel::LoadModel(const String& path)
{
	SetModelPath(path);

	if (!LoadRxModelDesc(path, mDesc)) {
		LogError(LC_ASSET, "Error parsing model config! (path: {})", path);
		return false;
	}

	RxBinaryFile mesh_file;

	if (!mesh_file.Open(ResolvePath(mDesc.Mesh.Path), "MESH") || !CheckFileHash(mesh_file, mDesc.Mesh)) {
		return false;
	}

	if (!BuildObjects(mesh_file)) {
		LogError(LC_ASSET, "Error loading model! (path: {})", path);
		return false;
	}

	return true;
}

bool LoaderRxModel::LoadWeaponPart(LoaderRxModel& part, const String& path, uint32 joint_offset, uint32 joint_count,
								   int32 static_joint)
{
	part.bKeepInMemory = bKeepInMemory;
	part.mpRootObject = mpRootObject;
	part.mpSkeleton = mpSkeleton;
	part.mJointOffset = joint_offset;
	part.mJointCount = joint_count;
	part.mStaticJoint = static_joint;
	part.mbIsPart = true;

	if (!part.LoadModel(path)) {
		return false;
	}

	if (part.mDesc.Clips.Size > 0) {
		LogInfo(LC_ASSET, "{}: animations of '{}' are ignored, a weapon plays the clips of its .rxweapon", mModelPath,
				path);
	}

	mRootChildren.insert(mRootChildren.end(), part.mRootChildren.begin(), part.mRootChildren.end());
	part.mRootChildren.clear();

	return true;
}

bool LoaderRxModel::LoadWeapon(const String& path)
{
	SetModelPath(path);

	RxWeaponDesc weapon;

	if (!LoadRxWeaponDesc(path, weapon)) {
		LogError(LC_ASSET, "Error parsing weapon config! (path: {})", path);
		return false;
	}

	if (!ReadSkeleton(weapon.Skeleton, mpSkeleton, mSkeletonSignature)) {
		return false;
	}

	mJointCount = mpSkeleton->JointCount;

	if (weapon.ViewhandsJoints + weapon.WeaponJoints != mJointCount) {
		LogError(LC_ASSET, "{}: '{}' has {} joints, expected {} viewhands and {} weapon joints", path,
				 ResolvePath(weapon.Skeleton.Path), mJointCount, weapon.ViewhandsJoints, weapon.WeaponJoints);
		return false;
	}

	const int32 attach_joint = FindJoint(weapon.AttachJoint.CStr());
	const int32 root_joint = FindJoint(weapon.RootJoint.CStr());

	if (attach_joint < 0 || static_cast<uint32>(attach_joint) >= weapon.ViewhandsJoints) {
		LogError(LC_ASSET, "{}: attach joint '{}' is not a viewhands joint of the rig", path, weapon.AttachJoint);
		return false;
	}

	if (root_joint < 0 || static_cast<uint32>(root_joint) < weapon.ViewhandsJoints ||
		mpSkeleton->ParentIndices[root_joint] != static_cast<uint32>(attach_joint)) {
		LogError(LC_ASSET, "{}: weapon root '{}' is not parented to '{}' in the rig", path, weapon.RootJoint,
				 weapon.AttachJoint);
		return false;
	}

	LoaderRxModel viewhands;
	LoaderRxModel model;

	if (!LoadWeaponPart(viewhands, ResolvePath(weapon.Viewhands.Path), 0, weapon.ViewhandsJoints, -1) ||
		!LoadWeaponPart(model, ResolvePath(weapon.Model.Path), weapon.ViewhandsJoints, weapon.WeaponJoints,
						root_joint)) {
		LogError(LC_ASSET, "Error loading weapon! (path: {})", path);
		return false;
	}

	LoadClips(weapon.Clips);

	LogInfo(LC_ASSET, "Loaded weapon '{}': {} viewhands joints, {} weapon joints, '{}' rides '{}'", path,
			weapon.ViewhandsJoints, weapon.WeaponJoints, weapon.RootJoint, weapon.AttachJoint);

	return true;
}

eLoaderStatus LoaderRxModel::Load(AssetTicket& ticket, const String& path)
{
	static constexpr std::string_view scWeaponExtension = ".rxweapon";

	const std::string_view view(path.CStr(), path.GetLength());

	const bool is_weapon = view.size() > scWeaponExtension.size() &&
						   view.substr(view.size() - scWeaponExtension.size()) == scWeaponExtension;

	mpRootObject = static_cast<Object*>(ticket.Get());

	return (is_weapon ? LoadWeapon(path) : LoadModel(path)) ? eLoaderStatus::Success : eLoaderStatus::Error;
}

eLoaderStatus LoaderRxModel::Load(AssetTicket& ticket, const uint8* data, uint32 size)
{
	LogError(LC_ASSET, "A .rxmodel links its binaries by relative path and can not be loaded from memory");
	return eLoaderStatus::Error;
}

void LoaderRxModel::UploadMeshToGpu(Object* object)
{
	if (object->pMesh.IsValid()) {
		Ref<PrimitiveMesh> primitive_mesh = object->pMesh;

		primitive_mesh->UploadIndices(GraphicsBackendFwd::GetUploadCmd());
		primitive_mesh->UploadVertices(GraphicsBackendFwd::GetUploadCmd());

		primitive_mesh->bIsReady = true;
	}
}

void LoaderRxModel::UploadObjectTree(Object* object)
{
	if (object == nullptr) {
		return;
	}

	UploadMeshToGpu(object);

	for (ObjectID child_id : object->AttachedNodes) {
		UploadObjectTree(gObjectManager->GetObject(child_id));
	}
}

void LoaderRxModel::CreateGpuResource(AssetTicket& ticket)
{
	Object* object = static_cast<Object*>(ticket.Get());

	for (ObjectID child_id : mRootChildren) {
		UploadObjectTree(gObjectManager->GetObject(child_id));
	}

	UploadMeshToGpu(object);
	ticket.SignalUploadedToGpu();
}

void LoaderRxModel::AttachChild(Object* parent, ObjectID child_id)
{
	if (parent == mpRootObject) {
		mRootChildren.push_back(child_id);
		return;
	}

	parent->AttachObject(child_id);
}

void LoaderRxModel::Publish(Object* root)
{
	for (ObjectID child_id : mRootChildren) {
		root->AttachObject(child_id);
	}

	mRootChildren.clear();
}

void LoaderRxModel::Destroy()
{
	mMaterialCache.clear();
	mWarned.clear();
}

} // namespace loader

} // namespace fx
