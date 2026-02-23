#include "PhMesh.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/MeshShape.h>


PhMesh::PhMesh(const FxPrimitiveMesh<>& mesh)
{
    FxAssert(mesh.VertexList.LocalBuffer.IsNotEmpty());

    const FxSizedArray<RxVertexDefault>& vertex_buffer = mesh.VertexList.LocalBuffer;
    const FxSizedArray<uint32>& index_buffer = mesh.LocalIndexBuffer;

    VertexList.reserve(vertex_buffer.Size);

    for (uint64 index = 0; index < vertex_buffer.Size; index++) {
        const RxVertexDefault& vertex = vertex_buffer.pData[index];

        VertexList.emplace_back(JPH::Float3 {
            vertex.Position[0],
            vertex.Position[1],
            vertex.Position[2],
        });
    }

    // Ensure that our vertex buffer is triangulated!
    FxAssert((index_buffer.Size % 3) == 0);

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
