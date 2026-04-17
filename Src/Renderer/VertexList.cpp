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


void VertexList::CreateFrom(const SizedArray<Vec3f>& positions, const SizedArray<Vec3f>& normals,
                            const SizedArray<Vec2f>& uvs, const SizedArray<Vec3f>& tangents,
                            const SizedArray<Vec4f>& bone_weights, const SizedArray<Vec4u>& bone_ids)
{
    Assert(mLocalBuffer.IsEmpty());

    VertexType = eVertexType::Slim;

    // Assume default vertex is requested if there are non-null values passed in for the additional components
    if (normals.IsInited() || uvs.IsInited() || tangents.IsInited()) {
        VertexType = eVertexType::Default;
    }

    // Only switch to the skinned vertex if there is animation data passed in. Otherwise, the caller should fallback to
    // the default vertex pipelines.
    if (bone_weights.IsNotEmpty() && bone_ids.IsNotEmpty()) {
        VertexType = eVertexType::Skinned;
    }

    const uint32 vertex_size = VertexUtil::GetSize(VertexType);

    mLocalBuffer.Create(vertex_size, positions.Size);

    const bool supports_default = (VertexType != eVertexType::Slim);
    const bool supports_skinning = (VertexType == eVertexType::Skinned);

    bContainsNormals = normals.IsNotEmpty();
    bContainsUVs = uvs.IsNotEmpty();
    bContainsTangents = tangents.IsNotEmpty();

    for (uint64 vertex_index = 0; vertex_index < mLocalBuffer.Capacity; vertex_index++) {
        Vertex<VertexLargestType> vertex;

        memcpy(vertex.Position, &positions[vertex_index].mData, sizeof(vertex.Position));

        // Write the components for a default vertex if the type supports it
        if (supports_default) {
            WriteOrZeroVec<float32, Vec3f, 3>(vertex.Normal, normals, vertex_index, bContainsNormals);
            WriteOrZeroVec<float32, Vec2f, 2>(vertex.UV, uvs, vertex_index, bContainsUVs);
            WriteOrZeroVec<float32, Vec3f, 3>(vertex.Tangent, tangents, vertex_index, bContainsTangents);

            if (supports_skinning) {
                // To support skinned vertices, we enforce that there is data available above; this means we dont need
                // to check in WriteOrZero.

                WriteOrZeroVec<uint32, Vec4u, 4>(vertex.BoneIds, bone_ids, vertex_index, true);
                WriteOrZeroVec<float32, Vec4f, 4>(vertex.BoneWeights, bone_weights, vertex_index, true);
            }
        }

        mLocalBuffer.InsertRaw(&vertex);
    }
}

void VertexList::CreateFrom(const SizedArray<float32>& positions, const SizedArray<float32>& normals,
                            const SizedArray<float32>& uvs, const SizedArray<float32>& tangents,
                            const SizedArray<float32>& bone_weights, const SizedArray<uint32>& bone_ids)
{
    Assert(mLocalBuffer.IsEmpty());
    Assert(positions.Size > 0);
    Assert(((positions.Size) % 3) == 0);

    VertexType = eVertexType::Slim;

    // Assume default vertex is requested if there are non-null values passed in for the additional components
    if (normals.IsInited() || uvs.IsInited() || tangents.IsInited()) {
        VertexType = eVertexType::Default;
    }

    // Only switch to the skinned vertex if there is animation data passed in. Otherwise, the caller should fallback to
    // the default vertex pipelines.
    if (bone_weights.IsNotEmpty() && bone_ids.IsNotEmpty()) {
        VertexType = eVertexType::Skinned;
    }

    const uint32 vertex_size = VertexUtil::GetSize(VertexType);
    const uint32 amount_of_vertices = positions.Size / 3;

    mLocalBuffer.Create(vertex_size, amount_of_vertices);

    const bool supports_default = (VertexType != eVertexType::Slim);
    const bool supports_skinning = (VertexType == eVertexType::Skinned);

    bContainsNormals = normals.IsNotEmpty();
    bContainsUVs = uvs.IsNotEmpty();
    bContainsTangents = tangents.IsNotEmpty();

    for (uint64 vertex_index = 0; vertex_index < mLocalBuffer.Capacity; vertex_index++) {
        Vertex<VertexLargestType> vertex;

        memcpy(vertex.Position, &positions[vertex_index * 3], sizeof(vertex.Position));

        // Write the components for a default vertex if the type supports it
        if (supports_default) {
            WriteOrZero<float32, 3>(vertex.Normal, normals, vertex_index, bContainsNormals);
            WriteOrZero<float32, 2>(vertex.UV, uvs, vertex_index, bContainsUVs);
            WriteOrZero<float32, 3>(vertex.Tangent, tangents, vertex_index, bContainsTangents);


            if (supports_skinning) {
                // To support skinned vertices, we enforce that there is data available above; this means we dont need
                // to check in WriteOrZero.
                WriteOrZero<uint32, 4>(vertex.BoneIds, bone_ids, vertex_index, true);
                WriteOrZero<float32, 4>(vertex.BoneWeights, bone_weights, vertex_index, true);
            }
        }

        mLocalBuffer.InsertRaw(&vertex);
    }
}


void VertexList::CreateSlimFrom(const SizedArray<float32>& positions)
{
    Assert(positions.Size > 0);
    Assert((positions.Size % 3) == 0);

    VertexType = eVertexType::Slim;

    mLocalBuffer.Create(sizeof(Vertex<eVertexType::Slim>), positions.Size / 3);

    for (int i = 0; i < mLocalBuffer.Capacity; i++) {
        Vertex<eVertexType::Slim> vertex;
        memcpy(&vertex.Position, &positions.pData[i * 3], sizeof(float32) * 3);

        mLocalBuffer.Insert(vertex);
    }
}

void VertexList::CreateSlimFrom(const SizedArray<Vec3f>& positions)
{
    Assert(positions.Size > 0);

    VertexType = eVertexType::Slim;

    mLocalBuffer.Create(sizeof(Vertex<eVertexType::Slim>), positions.Size);

    for (int i = 0; i < mLocalBuffer.Capacity; i++) {
        Vertex<eVertexType::Slim> vertex;
        memcpy(&vertex.Position, &positions.pData[i].mData, sizeof(float32) * 3);

        mLocalBuffer.Insert(vertex);
    }
}

} // namespace fx::renderer
