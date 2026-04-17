#pragma once

#include <Core/AnonArray.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/VertexList.hpp>
#include <cfloat>

namespace fx {

class MeshUtil
{
public:
    static Vec3f CalculateDimensions(const renderer::VertexList& vertex_list)
    {
        const AnonArray& vertices = vertex_list.GetLocalBuffer();

        if (vertices.IsEmpty()) {
            LogWarning("Cannot calculate dimensions as there are no vertices!");
            return Vec3f::sZero;
        }

        Vec3f min_vertex = Vec3f(FLT_MAX);
        Vec3f max_vertex = Vec3f(-FLT_MAX);

        for (uint32 i = 0; i < vertices.Size; i++) {
            // Since the position resides at the same location for each vertex type (see static_assert in
            // VertexUtil::GetPosition), the vertex type used here can be anything, and offsets are preserved as the
            // size of the object is stored in the anonymous buffer.
            Vec3f position = renderer::VertexUtil::GetPosition(
                *reinterpret_cast<const renderer::Vertex<renderer::VertexType::Default>*>(vertices.GetRaw(i)));

            min_vertex = Vec3f::Min(min_vertex, position);
            max_vertex = Vec3f::Max(max_vertex, position);
        }

        // Return the difference between the min and max positions
        max_vertex -= min_vertex;

        return max_vertex;
    }
};

} // namespace fx
