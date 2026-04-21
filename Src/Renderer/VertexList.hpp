#pragma once

#include "Backend/GpuBuffer.hpp"
#include "Vertex.hpp"

#include <Core/AnonArray.hpp>
#include <Core/SizedArray.hpp>

namespace fx {

enum class eVertexCreateFlags
{
    None = 0x00,
    NegativeX = 0x01,
};

FxEnumFlags(eVertexCreateFlags);

namespace renderer {

class VertexList
{
public:
    VertexList() = default;

    void CreateSlimFrom(const SizedArray<float32>& positions, eVertexCreateFlags create_flags);
    void CreateSlimFrom(const SizedArray<Vec3f>& positions, eVertexCreateFlags create_flags);

    void CreateFrom(const SizedArray<Vec3f>& positions, const SizedArray<Vec3f>& normals, const SizedArray<Vec2f>& uvs,
                    const SizedArray<Vec3f>& tangents, const SizedArray<Vec4f>& bone_weights,
                    const SizedArray<Vec4u>& bone_ids, eVertexCreateFlags create_flags);


    void CreateFrom(const SizedArray<float32>& positions, const SizedArray<float32>& normals,
                    const SizedArray<float32>& uvs, const SizedArray<float32>& tangents,
                    const SizedArray<float32>& bone_weights, const SizedArray<uint32>& bone_ids,
                    eVertexCreateFlags create_flags);

    template <renderer::eVertexType TVertexType>
    void CreateFrom(SizedArray<renderer::Vertex<TVertexType>>&& vertices)
    {
        VertexType = TVertexType;
        mLocalBuffer = std::move(vertices);
    }

    template <renderer::eVertexType TVertexType>
    void CreateAsCopyOf(const SizedArray<renderer::Vertex<TVertexType>>& vertices)
    {
        VertexType = TVertexType;
        mLocalBuffer.InitAsCopyOf(vertices);
    }

    void UploadToGpu() { GpuBuffer.Create(renderer::eGpuBufferType::VertexBuffer, mLocalBuffer); }

    /** @brief Returns true if the vertex type supports storing normals */
    FX_FORCE_INLINE bool SupportsNormals() const { return (VertexType != renderer::eVertexType::Slim); }

    /** @brief Returns true if the vertex buffer has been supplied values for normals. */
    FX_FORCE_INLINE bool HasNormals() const { return bContainsNormals; }

    FX_FORCE_INLINE bool IsSkinned() const { return VertexType == renderer::eVertexType::Skinned; }

    void DestroyLocalBuffer() { mLocalBuffer.Free(); }
    void Destroy()
    {
        GpuBuffer.Destroy();

        DestroyLocalBuffer();
    }

    AnonArray& GetLocalBuffer() { return mLocalBuffer; }
    const AnonArray& GetLocalBuffer() const { return mLocalBuffer; }


    ~VertexList() { Destroy(); }

public:
    renderer::eVertexType VertexType = renderer::eVertexType::Default;
    renderer::GpuBuffer GpuBuffer {};

    bool bContainsNormals : 1 = false;
    bool bContainsUVs : 1 = false;
    bool bContainsTangents : 1 = false;

private:
    AnonArray mLocalBuffer;
};

} // namespace renderer
} // namespace fx
