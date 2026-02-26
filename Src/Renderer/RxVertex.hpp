#pragma once

#include "Backend/RxGpuBuffer.hpp"

#include <Core/FxAnonArray.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>
#include <Math/FxVec2.hpp>
#include <Math/FxVec3.hpp>
#include <Renderer/FxMeshUtil.hpp>

enum class RxVertexType
{
    eSlim,
    eDefault,
    eSkinned
};


// Suppress clangd and its hate for #pragma pack
static_assert(true);

// Pack all of the structs below
#pragma pack(push, 1)

template <RxVertexType TType>
struct RxVertex
{
};

template <>
struct RxVertex<RxVertexType::eSlim>
{
public:
    float32 Position[3];

public:
    float32* GetPosition() { return Position; }
};

template <>
struct RxVertex<RxVertexType::eDefault>
{
public:
    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
    float32 Tangent[3];

public:
    float32* GetPosition() { return Position; }
};

template <>
struct RxVertex<RxVertexType::eSkinned>
{
    float32 Position[3];
    float32 Normal[3];
    float32 UV[2];
    float32 Tangent[3];
    uint32 BoneIds[4];      /// Skinning bone ids
    float32 BoneWeights[4]; /// Skinning bone weights

public:
    float32* GetPosition() { return Position; }
};


// End packing structs
#pragma pack(pop)

template <>
struct std::formatter<RxVertex<RxVertexType::eDefault>>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template <typename FmtContext>
    auto format(const RxVertex<RxVertexType::eDefault>& obj, FmtContext& ctx) const
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
    if constexpr (is_default_vertex) {                                                                                 \
        if (has_##member_name_) {                                                                                      \
            memcpy(&component_, &member_name_.pData[i * component_n_], sizeof(float32) * component_n_);                \
        }                                                                                                              \
        else {                                                                                                         \
            memset(&component_, 0, sizeof(float32) * component_n_);                                                    \
        }                                                                                                              \
    }

#define RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE_VEC3(component_, member_name_, component_n_)                           \
    if constexpr (is_default_vertex) {                                                                                 \
        if (has_##member_name_) {                                                                                      \
            memcpy(&component_, member_name_.pData[i].mData, sizeof(float32) * component_n_);                          \
        }                                                                                                              \
        else {                                                                                                         \
            memset(&component_, 0, sizeof(float32) * component_n_);                                                    \
        }                                                                                                              \
    }

constexpr bool FxIsComplexVertex(RxVertexType vertex_type)
{
    return (vertex_type == RxVertexType::eDefault || vertex_type == RxVertexType::eSkinned);
}

class RxVertexList
{
public:
    RxVertexList() = default;

    template <RxVertexType TVertexType>
    void CreateFrom(const FxSizedArray<float32>& positions, const FxSizedArray<float32>& normals,
                    const FxSizedArray<float32>& uvs, const FxSizedArray<float32>& tangents)
    {
        if (normals.IsNotEmpty()) {
            FxAssert((normals.Size == positions.Size));
        }

        const bool has_normals = (normals.IsNotEmpty());
        const bool has_uvs = (uvs.IsNotEmpty());
        const bool has_tangents = (tangents.IsNotEmpty());

        RxVertex<TVertexType> vertex;

        constexpr bool is_default_vertex = FxIsComplexVertex(TVertexType);

        // Create the local (cpu-side) buffer to store our vertices
        LocalBuffer.Create(sizeof(RxVertex<TVertexType>), positions.Size / 3);

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
            RxVertex<TVertexType> vertex;

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

    template <RxVertexType TVertexType>
    void CreateFrom(const FxSizedArray<FxVec3f>& positions, const FxSizedArray<FxVec3f>& normals,
                    const FxSizedArray<FxVec2f>& uvs, const FxSizedArray<FxVec3f>& tangents)
    {
        if (normals.IsNotEmpty()) {
            FxAssert((normals.Size == positions.Size));
        }

        const bool has_normals = (normals.IsNotEmpty());
        const bool has_uvs = (uvs.IsNotEmpty());
        const bool has_tangents = (tangents.IsNotEmpty());

        constexpr bool is_default_vertex = FxIsComplexVertex(TVertexType);

        // Create the local (cpu-side) buffer to store our vertices
        LocalBuffer.Create(sizeof(RxVertex<TVertexType>), positions.Size);

        if (!has_uvs) {
            FxLogWarning("Vertex list does not contain UV coordinates");
        }

        for (int i = 0; i < LocalBuffer.Capacity; i++) {
            RxVertex<TVertexType> vertex;

            memcpy(&vertex.Position, &positions.pData[i].mData, sizeof(float32) * 3);

            // If the normals are available (passed in and not null), then output them to the vertex object. If not,
            // zero them.
            RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE_VEC3(vertex.Normal, normals, 3);
            RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE_VEC3(vertex.UV, uvs, 2);
            RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE_VEC3(vertex.Tangent, tangents, 3);

            LocalBuffer.Insert(vertex);
        }
    }

    // void CreateFrom(const FxSizedArray<float32>& positions) { CreateFrom(positions, {}, {}, {}); }
    // void CreateFrom(const FxSizedArray<FxVec3f>& positions) { CreateFrom(positions, {}, {}, {}); }

    void UploadToGpu() { GpuBuffer.Create(RxGpuBufferType::eVertexBuffer, LocalBuffer); }

    void DestroyLocalBuffer() { LocalBuffer.Free(); }
    void Destroy()
    {
        GpuBuffer.Destroy();

        DestroyLocalBuffer();
    }


    ~RxVertexList() { Destroy(); }

public:
    RxGpuBuffer GpuBuffer {};
    // FxSizedArray<TVertexType> LocalBuffer {};
    FxAnonArray LocalBuffer;

    bool bContainsNormals : 1 = false;
    bool bContainsUVs : 1 = false;
    bool bContainsTangents : 1 = false;
};

#undef RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE
#undef RX_VERTEX_OUTPUT_COMPONENT_IF_AVAILABLE_VEC3
