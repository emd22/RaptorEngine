#include "PhMesh.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/MeshShape.h>

namespace fx {

PhMesh::PhMesh(const PrimitiveMesh& mesh)
{
    const AnonArray& positions = mesh.VertexList.GetLocalBuffer();
    const SizedArray<uint32>& index_buffer = mesh.LocalIndexBuffer;

    VertexList.reserve(positions.Size);

    for (uint32 index = 0; index < positions.Size; index++) {
        const float32* position = static_cast<const float32*>(positions.GetRaw(index));

        VertexList.emplace_back(JPH::Float3 {
            position[0],
            position[1],
            position[2],
        });
    }

    TriangleList.reserve(index_buffer.Size / 3);

    for (uint64 index = 0; index + 2 < index_buffer.Size; index += 3) {
        const uint32 a = index_buffer.pData[index];
        const uint32 b = index_buffer.pData[index + 1];
        const uint32 c = index_buffer.pData[index + 2];

        if (a >= positions.Size || b >= positions.Size || c >= positions.Size) {
            continue;
        }

        TriangleList.emplace_back(JPH::IndexedTriangle { a, b, c });
    }
}


JPH::MeshShapeSettings PhMesh::GetShapeSettings() const { return JPH::MeshShapeSettings(VertexList, TriangleList); }

} // namespace fx
