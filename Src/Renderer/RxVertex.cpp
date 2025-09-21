#include "RxVertex.hpp"

#include <Math/FxVec3.hpp>

using VertexType = RxVertexList::VertexType;


/**
 * @brief Output the component to the a local RxVertex object if a local variable `can_output_{member_name_}` is true.
 * If `has_{member_name_}` is true, then this macro copies the data into the proper member of the object. If it is not
 * true, this macro zeros the member.
 */
#define OUTPUT_COMPONENT_IF_AVAILABLE(component_, member_name_, component_n_)                                          \
    if (can_output_##member_name_) {                                                                                   \
        if (has_##member_name_) {                                                                                      \
            memcpy(&component_, &member_name_.Data[i * component_n_], sizeof(float32) * component_n_);                 \
        }                                                                                                              \
        else {                                                                                                         \
            memset(&component_, 0, sizeof(float32) * component_n_);                                                    \
        }                                                                                                              \
    }

void RxVertexList::Create(const FxSizedArray<float32>& positions, const FxSizedArray<float32>& normals,
                          const FxSizedArray<float32>& uvs)
{
    FxAssert((normals.Size == positions.Size));

    const bool has_normals = (normals.Size != 0);
    const bool has_uvs = (uvs.Size != 0);

    constexpr bool can_output_normals = (VertexType::Components & FxVertexNormal);
    constexpr bool can_output_uvs = (VertexType::Components & FxVertexUV);

    // Create the local (cpu-side) buffer to store our vertices
    mLocalBuffer.InitCapacity(positions.Size / 3);

    if (!has_uvs) {
        FxLogWarning("Vertex list does not contain UV coordinates", 0);
    }

    for (int i = 0; i < mLocalBuffer.Capacity; i++) {
        VertexType vertex;

        memcpy(&vertex.Position, &positions.Data[i * 3], sizeof(float32) * 3);

        // If the normals are available (passed in and not null), then output them to the vertex object. If not, zero
        // them.
        OUTPUT_COMPONENT_IF_AVAILABLE(vertex.Normal, normals, 3);
        OUTPUT_COMPONENT_IF_AVAILABLE(vertex.UV, uvs, 2);

        mLocalBuffer.Insert(vertex);
    }
}

void RxVertexList::Create(const FxSizedArray<float32>& positions) { Create(positions, {}, {}); }

FxVec3f RxVertexList::CalculateDimensionsFromPositions()
{
    if (mLocalBuffer.IsEmpty()) {
        FxLogWarning("Cannot calculate dimensions as the local vertex buffer has been cleared!");

        return FxVec3f::Zero;
    }

    FxVec3f min_positions = FxVec3f(10000);
    FxVec3f max_positions = FxVec3f(-10000);

    for (int i = 0; i < mLocalBuffer.Size; i++) {
        FxVec3f position(mLocalBuffer[i].Position);

        min_positions = FxVec3f::Min(min_positions, position);
        max_positions = FxVec3f::Max(max_positions, position);
    }

    // Return the difference between the min and max positions
    max_positions -= min_positions;

    return max_positions;
}


void RxVertexList::Destroy()
{
    mGpuBuffer.Destroy();
    mLocalBuffer.Free();
}
