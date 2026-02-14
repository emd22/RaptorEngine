#pragma once

#include <Core/FxSizedArray.hpp>
#include <Math/FxVec3.hpp>

class FxMeshUtil
{
public:
    template <typename TVertexType>
    static FxVec3f CalculateDimensions(const FxSizedArray<TVertexType>& vertices)
    {
        if (vertices.IsEmpty()) {
            FxLogWarning("Cannot calculate dimensions as there are no vertices!");
            return FxVec3f::sZero;
        }

        FxVec3f min_vertex = FxVec3f(10000);
        FxVec3f max_vertex = FxVec3f(-10000);

        for (int i = 0; i < vertices.Size; i++) {
            if constexpr (std::is_same_v<TVertexType, float32>) {
                const FxVec3f position = FxVec3f(&vertices[i * 3]);

                min_vertex = FxVec3f::Min(min_vertex, position);
                max_vertex = FxVec3f::Max(max_vertex, position);
            }
            else {
                const FxVec3f& position = vertices[i].Position;

                min_vertex = FxVec3f::Min(min_vertex, position);
                max_vertex = FxVec3f::Max(max_vertex, position);
            }
        }

        // Return the difference between the min and max positions
        max_vertex -= min_vertex;

        return max_vertex;
    }
};
