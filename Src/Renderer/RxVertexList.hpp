#pragma once

#include "Backend/RxGpuBuffer.hpp"
#include "RxVertex.hpp"

#include <Core/AnonArray.hpp>
#include <Core/SizedArray.hpp>

namespace fx::renderer {

class RxVertexList
{
public:
    RxVertexList() = default;

    void CreateSlimFrom(const SizedArray<float32>& positions);
    void CreateSlimFrom(const SizedArray<Vec3f>& positions);

    void CreateFrom(const SizedArray<Vec3f>& positions, const SizedArray<Vec3f>& normals, const SizedArray<Vec2f>& uvs,
                    const SizedArray<Vec3f>& tangents, const SizedArray<Vec4f>& bone_weights,
                    const SizedArray<Vec4u>& bone_ids);


    void CreateFrom(const SizedArray<float32>& positions, const SizedArray<float32>& normals,
                    const SizedArray<float32>& uvs, const SizedArray<float32>& tangents,
                    const SizedArray<float32>& bone_weights, const SizedArray<uint32>& bone_ids);

    template <renderer::RxVertexType TVertexType>
    void CreateFrom(SizedArray<renderer::RxVertex<TVertexType>>&& vertices)
    {
        VertexType = TVertexType;
        mLocalBuffer = std::move(vertices);
    }

    template <renderer::RxVertexType TVertexType>
    void CreateAsCopyOf(const SizedArray<renderer::RxVertex<TVertexType>>& vertices)
    {
        VertexType = TVertexType;
        mLocalBuffer.InitAsCopyOf(vertices);
    }

    void UploadToGpu() { GpuBuffer.Create(renderer::RxGpuBufferType::eVertexBuffer, mLocalBuffer); }

    /** @brief Returns true if the vertex type supports storing normals */
    FX_FORCE_INLINE bool SupportsNormals() const { return (VertexType != renderer::RxVertexType::eSlim); }

    /** @brief Returns true if the vertex buffer has been supplied values for normals. */
    FX_FORCE_INLINE bool HasNormals() const { return bContainsNormals; }

    FX_FORCE_INLINE bool IsSkinned() const { return VertexType == renderer::RxVertexType::eSkinned; }

    void DestroyLocalBuffer() { mLocalBuffer.Free(); }
    void Destroy()
    {
        GpuBuffer.Destroy();

        DestroyLocalBuffer();
    }

    AnonArray& GetLocalBuffer() { return mLocalBuffer; }
    const AnonArray& GetLocalBuffer() const { return mLocalBuffer; }


    ~RxVertexList() { Destroy(); }

public:
    renderer::RxVertexType VertexType = renderer::RxVertexType::eDefault;
    renderer::RxGpuBuffer GpuBuffer {};

    bool bContainsNormals : 1 = false;
    bool bContainsUVs : 1 = false;
    bool bContainsTangents : 1 = false;

private:
    AnonArray mLocalBuffer;
};

} // namespace fx::renderer
