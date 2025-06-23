#pragma once

#include "Backend/RvkFrameData.hpp"
#include "Backend/RvkGpuBuffer.hpp"
#include "Renderer/Renderer.hpp"

#include <atomic>

class FxMesh
{
public:
    FxMesh() = default;

    using VertexType = RvkVertex<FxVertexPosition | FxVertexNormal | FxVertexUV>;

    FxMesh(FxSizedArray<VertexType> &vertices)
    {
        UploadVertices(vertices);
    }

    FxMesh(FxSizedArray<VertexType> &vertices, FxSizedArray<uint32> &indices)
    {
        CreateFromData(vertices, indices);
    }

    // FxMesh(const FxMesh &other)
    // {
    //     *this = other;
    // }

    // FxMesh& operator=(const FxMesh& other)
    // {
    //     mVertexBuffer = std::move(other.mVertexBuffer);
    //     mIndexBuffer = std::move(other.mIndexBuffer);
    //     return *this;
    // }

    void CreateFromData(FxSizedArray<VertexType> &vertices, FxSizedArray<uint32> &indices)
    {
        UploadVertices(vertices);
        UploadIndices(indices);
    }

    void UploadVertices(FxSizedArray<VertexType> &vertices)
    {
        mVertexBuffer.Create(RvkBufferUsageType::Vertices, vertices);
    }

    void UploadIndices(FxSizedArray<uint32> &indices)
    {
        mIndexBuffer.Create(RvkBufferUsageType::Indices, indices);
    }

    // FxMesh(FxMesh &&other)
    // {
    //     mIndexBuffer = std::move(other.mIndexBuffer);
    //     mVertexBuffer = std::move(other.mVertexBuffer);

    // }

    // FxMesh &operator=(FxMesh &&other)
    // {
    //     mIndexBuffer = std::move(other.mIndexBuffer);
    //     mVertexBuffer = std::move(other.mVertexBuffer);
    //     return *this;
    // }

    static FxSizedArray<VertexType> MakeCombinedVertexBuffer(const FxSizedArray<float32>& positions, const FxSizedArray<float32>& normals, const FxSizedArray<float32>& uvs)
    {
        FxAssert((normals.Size == positions.Size));

        FxSizedArray<VertexType> vertices(positions.Size / 3);

        // Log::Info("Creating combined vertex buffer (s: %d)", vertices.Capacity);
        const bool has_texcoords = uvs.Size > 0;

        if (!has_texcoords) {
            Log::Info("Model does not have texture coordinates!", 0);
        }

        for (int i = 0; i < vertices.Capacity; i++) {
            // make a combined vertex + normal + other a
            VertexType vertex;

            memcpy(&vertex.Position, &positions.Data[i * 3], sizeof(float32) * 3);
            memcpy(&vertex.Normal, &normals.Data[i * 3], sizeof(float32) * 3);

            if (has_texcoords) {
                memcpy(&vertex.UV, &uvs.Data[i * 2], sizeof(float32) * 2);
            }
            else {
                memset(&vertex.UV, 0, sizeof(float32) * 2);
            }

            vertices.Insert(vertex);
        }

        return vertices;
    }

    bool IsWritable()
    {
        return (mVertexBuffer.Initialized && mIndexBuffer.Initialized);
    }

    RvkGpuBuffer<VertexType> &GetVertexBuffer() { return mVertexBuffer; }
    RvkGpuBuffer<uint32> &GetIndexBuffer() { return mIndexBuffer; }

    void Render(RvkGraphicsPipeline &pipeline)
    {
        const VkDeviceSize offset = 0;
        RvkFrameData *frame = Renderer->GetFrame();

        vkCmdBindVertexBuffers(frame->CommandBuffer.CommandBuffer, 0, 1, &mVertexBuffer.Buffer, &offset);
        vkCmdBindIndexBuffer(frame->CommandBuffer.CommandBuffer, mIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);


        vkCmdDrawIndexed(frame->CommandBuffer.CommandBuffer, static_cast<uint32>(mIndexBuffer.Size), 1, 0, 0, 0);
        // vkCmdDraw(frame->CommandBuffer.CommandBuffer, mVertexBuffer.Size, 1, 0, 0);
    }

    void Destroy()
    {
        if (IsReady == false) {
            return;
        }
        IsReady = false;

        mVertexBuffer.Destroy();
        mIndexBuffer.Destroy();
    }

    ~FxMesh()
    {
        Destroy();
    }

    std::atomic_bool IsReady = std::atomic_bool(false);

protected:
    RvkGpuBuffer<VertexType> mVertexBuffer;
    RvkGpuBuffer<uint32> mIndexBuffer;
};
