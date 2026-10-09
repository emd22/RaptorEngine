#include "VertexList.hpp"

namespace fx::renderer {

template <typename TType, uint32 TNumComponents>
FX_FORCE_INLINE void WriteOrZero(TType* dst, const SizedArray<TType>& src, uint64 index, bool write_or_zero)
{
	constexpr uint32 size = TNumComponents * sizeof(TType);

	if (write_or_zero) {
		memcpy(dst, &src[index * TNumComponents], size);
		return;
	}

	memset(dst, 0, size);
}

template <typename TType, typename TVecType, uint32 TNumComponents>
FX_FORCE_INLINE void WriteOrZeroVec(TType* dst, const SizedArray<TVecType>& src, uint64 index, bool write_or_zero)
{
	constexpr uint32 size = TNumComponents * sizeof(TType);

	if (write_or_zero) {
		memcpy(dst, &src[index].mData, size);
		return;
	}

	memset(dst, 0, size);
}

static eVertexType ChooseVertexType(bool has_default_data, bool has_skinning, eVertexCreateFlags create_flags)
{
	eVertexType type = eVertexType::Slim;

	if (has_default_data || (create_flags & eVertexCreateFlags::DefaultLayout) != 0) {
		type = eVertexType::Default;
	}

	if (has_skinning) {
		type = eVertexType::Skinned;
	}

	return type;
}

void VertexList::CreateFrom(const SizedArray<Vec3f>& positions, const SizedArray<Vec3f>& normals,
							const SizedArray<Vec2f>& uvs, const SizedArray<Vec3f>& tangents,
							const SizedArray<Vec4f>& bone_weights, const SizedArray<Vec4u>& bone_ids,
							eVertexCreateFlags create_flags, const SizedArray<float32>& tangent_handedness)
{
	Assert(mLocalBuffer.IsEmpty());

	const uint64 vertex_count = positions.Size;

	const bool has_normals = normals.Size >= vertex_count;
	const bool has_uvs = uvs.Size >= vertex_count;
	const bool has_tangents = tangents.Size >= vertex_count;
	const bool has_handedness = tangent_handedness.Size >= vertex_count;
	const bool has_skinning = bone_weights.Size >= vertex_count && bone_ids.Size >= vertex_count;

	VertexType = ChooseVertexType(has_normals || has_uvs || has_tangents, has_skinning, create_flags);

	const uint32 vertex_size = VertexUtil::GetSize(VertexType);

	mLocalBuffer.Create(vertex_size, positions.Size);

	const bool supports_default = (VertexType != eVertexType::Slim);
	const bool supports_skinning = (VertexType == eVertexType::Skinned);
	const bool negate_x = (create_flags & eVertexCreateFlags::NegativeX) != 0;

	bContainsNormals = has_normals;
	bContainsUVs = has_uvs;
	bContainsTangents = has_tangents;

	for (uint64 vertex_index = 0; vertex_index < mLocalBuffer.Capacity; vertex_index++) {
		Vertex<VertexLargestType> vertex;

		memcpy(vertex.Position, &positions[vertex_index].mData, sizeof(vertex.Position));

		if (negate_x) {
			vertex.Position[0] = -vertex.Position[0];
		}

		if (supports_default) {
			WriteOrZeroVec<float32, Vec3f, 3>(vertex.Normal, normals, vertex_index, has_normals);
			WriteOrZeroVec<float32, Vec2f, 2>(vertex.UV, uvs, vertex_index, has_uvs);
			WriteOrZeroVec<float32, Vec3f, 3>(vertex.Tangent, tangents, vertex_index, has_tangents);

			vertex.Tangent[3] = has_handedness ? tangent_handedness[vertex_index] : 1.0f;

			if (negate_x) {
				vertex.Normal[0] = -vertex.Normal[0];
				vertex.Tangent[0] = -vertex.Tangent[0];
				vertex.Tangent[3] = -vertex.Tangent[3];
			}

			if (supports_skinning) {
				WriteOrZeroVec<uint32, Vec4u, 4>(vertex.BoneIds, bone_ids, vertex_index, true);
				WriteOrZeroVec<float32, Vec4f, 4>(vertex.BoneWeights, bone_weights, vertex_index, true);
			}
		}

		mLocalBuffer.InsertRaw(&vertex);
	}
}

void VertexList::CreateFrom(const SizedArray<float32>& positions, const SizedArray<float32>& normals,
							const SizedArray<float32>& uvs, const SizedArray<float32>& tangents,
							const SizedArray<float32>& bone_weights, const SizedArray<uint32>& bone_ids,
							eVertexCreateFlags create_flags)
{
	Assert(mLocalBuffer.IsEmpty());
	Assert(positions.Size > 0);
	Assert(((positions.Size) % 3) == 0);

	const uint32 amount_of_vertices = positions.Size / 3;

	const bool has_normals = normals.Size >= static_cast<uint64>(amount_of_vertices) * 3;
	const bool has_uvs = uvs.Size >= static_cast<uint64>(amount_of_vertices) * 2;
	const bool has_tangents = tangents.Size >= static_cast<uint64>(amount_of_vertices) * 4;
	const bool has_skinning = bone_weights.Size >= static_cast<uint64>(amount_of_vertices) * 4 &&
							  bone_ids.Size >= static_cast<uint64>(amount_of_vertices) * 4;

	VertexType = ChooseVertexType(has_normals || has_uvs || has_tangents, has_skinning, create_flags);

	const uint32 vertex_size = VertexUtil::GetSize(VertexType);

	mLocalBuffer.Create(vertex_size, amount_of_vertices);

	const bool supports_default = (VertexType != eVertexType::Slim);
	const bool supports_skinning = (VertexType == eVertexType::Skinned);
	const bool negate_x = (create_flags & eVertexCreateFlags::NegativeX) != 0;

	bContainsNormals = has_normals;
	bContainsUVs = has_uvs;
	bContainsTangents = has_tangents;

	for (uint64 vertex_index = 0; vertex_index < mLocalBuffer.Capacity; vertex_index++) {
		Vertex<VertexLargestType> vertex;

		memcpy(vertex.Position, &positions[vertex_index * 3], sizeof(vertex.Position));

		if (negate_x) {
			vertex.Position[0] = -vertex.Position[0];
		}

		if (supports_default) {
			WriteOrZero<float32, 3>(vertex.Normal, normals, vertex_index, has_normals);
			WriteOrZero<float32, 2>(vertex.UV, uvs, vertex_index, has_uvs);
			WriteOrZero<float32, 4>(vertex.Tangent, tangents, vertex_index, has_tangents);

			if (!has_tangents) {
				vertex.Tangent[3] = 1.0f;
			}

			if (negate_x) {
				vertex.Normal[0] = -vertex.Normal[0];
				vertex.Tangent[0] = -vertex.Tangent[0];
				vertex.Tangent[3] = -vertex.Tangent[3];
			}

			if (supports_skinning) {
				WriteOrZero<uint32, 4>(vertex.BoneIds, bone_ids, vertex_index, true);
				WriteOrZero<float32, 4>(vertex.BoneWeights, bone_weights, vertex_index, true);
			}
		}

		mLocalBuffer.InsertRaw(&vertex);
	}
}


void VertexList::CreateSlimFrom(const SizedArray<float32>& positions, eVertexCreateFlags create_flags)
{
	Assert(positions.Size > 0);
	Assert((positions.Size % 3) == 0);

	VertexType = eVertexType::Slim;

	mLocalBuffer.Create(sizeof(Vertex<eVertexType::Slim>), positions.Size / 3);

	for (int i = 0; i < mLocalBuffer.Capacity; i++) {
		Vertex<eVertexType::Slim> vertex;
		memcpy(&vertex.Position, &positions.pData[i * 3], sizeof(float32) * 3);

		if ((create_flags & eVertexCreateFlags::NegativeX) != 0) {
			vertex.Position[0] = -vertex.Position[0];
		}

		mLocalBuffer.Insert(vertex);
	}
}

void VertexList::CreateSlimFrom(const SizedArray<Vec3f>& positions, eVertexCreateFlags create_flags)
{
	Assert(positions.Size > 0);

	VertexType = eVertexType::Slim;

	mLocalBuffer.Create(sizeof(Vertex<eVertexType::Slim>), positions.Size);

	for (int i = 0; i < mLocalBuffer.Capacity; i++) {
		Vertex<eVertexType::Slim> vertex;
		memcpy(&vertex.Position, &positions.pData[i].mData, sizeof(float32) * 3);

		if ((create_flags & eVertexCreateFlags::NegativeX) != 0) {
			vertex.Position[0] = -vertex.Position[0];
		}

		mLocalBuffer.Insert(vertex);
	}
}

} // namespace fx::renderer
