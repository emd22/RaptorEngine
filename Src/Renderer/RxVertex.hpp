#pragma once

#include "Backend/RxGpuBuffer.hpp"

#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>
#include <Math/FxVec2.hpp>
#include <Math/FxVec3.hpp>
#include <Renderer/FxMeshUtil.hpp>

enum FxVertexFlags : int8
{
    FxVertexPosition = 0x01,
    FxVertexNormal = 0x02,
    FxVertexUV = 0x04,
    FxVertexTangent = 0x08,
};

FX_DEFINE_ENUM_AS_FLAGS(FxVertexFlags);


// Pack all of the structs below
#pragma pack(push, 1)

template <FxVertexFlags TComponents>
struct RxVertex;

template <>
struct RxVertex<FxVertexPosition>
{
    static constexpr FxVertexFlags Components = FxVertexPosition;

    float32 Position[3];
};

template <>
struct RxVertex<FxVertexPosition | FxVertexNormal>
{
    static constexpr FxVertexFlags Components = (FxVertexPosition | FxVertexNormal);

    float32 Position[3];
    float32 Normal[3];
};

template <>
struct RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>
{
    static constexpr FxVertexFlags Components = (FxVertexPosition | FxVertexNormal | FxVertexUV);

    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
};

template <>
struct RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV | FxVertexTangent>
{
    static constexpr FxVertexFlags Components = (FxVertexPosition | FxVertexNormal | FxVertexUV | FxVertexTangent);

    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
    float32 Tangent[3];
};

// End packing structs
#pragma pack(pop)

using RxVertexDefault = RxVertex<FxVertexPosition | FxVertexNormal | FxVertexUV | FxVertexTangent>;

template <>
struct std::formatter<RxVertexDefault>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template <typename FmtContext>
    auto format(const RxVertexDefault& obj, FmtContext& ctx) const
    {
        return std::format_to(ctx.out(), "( {:.04}, {:.04}, {:.04} )", static_cast<float>(obj.Position[0]),
                              static_cast<float>(obj.Position[1]), static_cast<float>(obj.Position[2]));
    }
};

/**
 * @brief Output the component to the a local RxVertex object if a local variable `can_output_{member_name_}` is true.
 * If `has_{member_name_}` is true, then this macro copies the data into the proper member of the object. If it is not
 * true, this macro zeros the member.
 */
#define RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE(component_, member_name_, component_n_)                                \
    if constexpr (can_output_##member_name_) {                                                                         \
        if (has_##member_name_) {                                                                                      \
            memcpy(&component_, &member_name_.pData[i * component_n_], sizeof(float32) * component_n_);                \
        }                                                                                                              \
        else {                                                                                                         \
            memset(&component_, 0, sizeof(float32) * component_n_);                                                    \
        }                                                                                                              \
    }

#define RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE_VEC3(component_, member_name_, component_n_)                           \
    if constexpr (can_output_##member_name_) {                                                                         \
        if (has_##member_name_) {                                                                                      \
            memcpy(&component_, member_name_.pData[i].mData, sizeof(float32) * component_n_);                          \
        }                                                                                                              \
        else {                                                                                                         \
            memset(&component_, 0, sizeof(float32) * component_n_);                                                    \
        }                                                                                                              \
    }

template <typename TVertexType>
class RxVertexList
{
public:
    using VertexType = TVertexType;

public:
    RxVertexList() = default;

    void CreateFrom(const FxSizedArray<float32>& positions, const FxSizedArray<float32>& normals,
                    const FxSizedArray<float32>& uvs, const FxSizedArray<float32>& tangents)
    {
        if (normals.IsNotEmpty()) {
            FxAssert((normals.Size == positions.Size));
        }

        const bool has_normals = (normals.IsNotEmpty());
        const bool has_uvs = (uvs.IsNotEmpty());
        const bool has_tangents = (tangents.IsNotEmpty());

        constexpr bool can_output_normals = (TVertexType::Components & FxVertexNormal);
        constexpr bool can_output_uvs = (TVertexType::Components & FxVertexUV);
        constexpr bool can_output_tangents = (TVertexType::Components & FxVertexTangent);

        // Create the local (cpu-side) buffer to store our vertices
        LocalBuffer.InitCapacity(positions.Size / 3);

        if (!has_uvs) {
            FxLogWarning("Vertex list does not contain UV coordinates", 0);
        }

        // Note that if can_output_* is true but has_* is false, that field will be zeroed out.

        if (has_normals) {
            bContainsNormals = true;
        }
        if (has_uvs) {
            bContainsUVs = true;
        }
        if (has_tangents) {
            bContainsTangents = true;
        }

        for (int i = 0; i < LocalBuffer.Capacity; i++) {
            TVertexType vertex;

            memcpy(&vertex.Position, &positions.pData[i * 3], sizeof(float32) * 3);

            RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE(vertex.Normal, normals, 3);
            // if constexpr (can_output_normals) {
            //     if (has_normals) {
            //         memcpy(&vertex.Normal, &normals.Data[i], sizeof(float32) * 3);
            //     }
            //     else {
            //         GenerateNormalForPosition(vertex.Position, vertex.Normal);
            //     }
            // }


            // If the normals are available (passed in and not null), then output them to the vertex object. If not,
            // zero them.
            RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE(vertex.UV, uvs, 2);
            RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE(vertex.Tangent, tangents, 3);

            LocalBuffer.Insert(vertex);
        }
    }

    void CreateFrom(const FxSizedArray<FxVec3f>& positions, const FxSizedArray<FxVec3f>& normals,
                    const FxSizedArray<FxVec2f>& uvs, const FxSizedArray<FxVec3f>& tangents)
    {
        if (normals.IsNotEmpty()) {
            FxAssert((normals.Size == positions.Size));
        }

        const bool has_normals = (normals.IsNotEmpty());
        const bool has_uvs = (uvs.IsNotEmpty());
        const bool has_tangents = (tangents.IsNotEmpty());

        constexpr bool can_output_normals = (TVertexType::Components & FxVertexNormal);
        constexpr bool can_output_uvs = (TVertexType::Components & FxVertexUV);
        constexpr bool can_output_tangents = (TVertexType::Components & FxVertexTangent);

        // Create the local (cpu-side) buffer to store our vertices
        LocalBuffer.InitCapacity(positions.Size);

        if (!has_uvs) {
            FxLogWarning("Vertex list does not contain UV coordinates", 0);
        }

        for (int i = 0; i < LocalBuffer.Capacity; i++) {
            TVertexType vertex;

            memcpy(&vertex.Position, &positions.pData[i].mData, sizeof(float32) * 3);

            // If the normals are available (passed in and not null), then output them to the vertex object. If not,
            // zero them.
            RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE_VEC3(vertex.Normal, normals, 3);
            RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE_VEC3(vertex.UV, uvs, 2);
            RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE_VEC3(vertex.Tangent, tangents, 3);

            LocalBuffer.Insert(vertex);
        }
    }

    void CreateFrom(const FxSizedArray<float32>& positions) { CreateFrom(positions, {}, {}, {}); }
    void CreateFrom(const FxSizedArray<FxVec3f>& positions) { CreateFrom(positions, {}, {}, {}); }

    void UploadToGpu()
    {
        FxLogInfo("Uploading vertex list to GPU buffer");
        GpuBuffer.Create(RxGpuBufferType::eVertexBuffer, LocalBuffer);
    }

    FxVec3f CalculateDimensionsFromPositions()
    {
        return FxMeshUtil::CalculateDimensions(LocalBuffer);
        // if (LocalBuffer.IsEmpty()) {
        //     FxLogWarning("Cannot calculate dimensions as the local vertex buffer has been cleared!");

        //     return FxVec3f::sZero;
        // }

        // FxVec3f min_positions = FxVec3f(10000);
        // FxVec3f max_positions = FxVec3f(-10000);

        // for (int i = 0; i < LocalBuffer.Size; i++) {
        //     const FxVec3f& position = LocalBuffer[i].Position;

        //     min_positions = FxVec3f::Min(min_positions, position);
        //     max_positions = FxVec3f::Max(max_positions, position);
        // }

        // FxLogInfo("Max Positions: {}, Min Positions: {}", max_positions, min_positions);

        // // Return the difference between the min and max positions
        // max_positions -= min_positions;

        // return max_positions;
    }

    void DestroyLocalBuffer() { LocalBuffer.Free(); }
    void Destroy()
    {
        GpuBuffer.Destroy();

        DestroyLocalBuffer();
    }


    ~RxVertexList() { Destroy(); }

public:
    FxVec3f Dimensions = FxVec3f::sZero;

    RxGpuBuffer GpuBuffer {};
    FxSizedArray<TVertexType> LocalBuffer {};

    bool bContainsNormals : 1 = false;
    bool bContainsUVs : 1 = false;
    bool bContainsTangents : 1 = false;
};

#undef RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE
#undef RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE_VEC3
