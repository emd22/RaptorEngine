#include "PhMesh.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/MeshShape.h>

namespace fx {

PhMesh::PhMesh(const PrimitiveMesh& mesh)
{
    const AnonArray& positions = mesh.VertexList.GetLocalBuffer();
    Assert(positions.IsNotEmpty());

    const SizedArray<uint32>& index_buffer = mesh.LocalIndexBuffer;

    VertexList.reserve(positions.Size);

    for (uint64 index = 0; index < positions.Size; index++) {
        const renderer::Vertex<renderer::VertexType::Default>& vertex =
            positions.Get<renderer::Vertex<renderer::VertexType::Default>>(index);

        VertexList.emplace_back(JPH::Float3 {
            vertex.Position[0],
            vertex.Position[1],
            vertex.Position[2],
        });
    }

    // Ensure that our vertex buffer is triangulated!
    Assert((index_buffer.Size % 3) == 0);

    TriangleList.reserve(index_buffer.Size / 3);

    for (uint64 index = 0; index < index_buffer.Size; index += 3) {
        TriangleList.emplace_back(JPH::IndexedTriangle {
            index_buffer.pData[index],
            index_buffer.pData[index + 1],
            index_buffer.pData[index + 2],
        });
    }
}


JPH::MeshShapeSettings PhMesh::GetShapeSettings() const { return JPH::MeshShapeSettings(VertexList, TriangleList); }

} // namespace fx
