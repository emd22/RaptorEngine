#pragma once

#include "Renderer/Backend/Vulkan/RvkFrameData.hpp"
#include "Renderer/Renderer.hpp"
#include "vulkan/vulkan_core.h"
#include <Renderer/Backend/Vulkan/RvkGpuBuffer.hpp>
#include <atomic>

// TEMP
using namespace vulkan;


class FxMesh
{
public:
    FxMesh() = default;

    using VertexType = RvkVertex<FxVertexPosition | FxVertexNormal>;

    FxMesh(FxStaticArray<VertexType> &vertices)
    {
        UploadVertices(vertices);
    }

    FxMesh(FxStaticArray<VertexType> &vertices, FxStaticArray<uint32> &indices)
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

    void CreateFromData(FxStaticArray<VertexType> &vertices, FxStaticArray<uint32> &indices)
    {
        UploadVertices(vertices);
        UploadIndices(indices);
    }

    void UploadVertices(FxStaticArray<VertexType> &vertices)
    {
        mVertexBuffer.Create(RvkBufferUsageType::Vertices, vertices);
    }

    void UploadIndices(FxStaticArray<uint32> &indices)
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

    static FxStaticArray<VertexType> MakeCombinedVertexBuffer(FxStaticArray<float32> &positions)
    {
        FxStaticArray<VertexType> vertices(positions.Size / 3);

        // Log::Info("Creating combined vertex buffer (Contains:Position) (s: %d)", vertices.Capacity);

        for (int i = 0; i < vertices.Capacity; i++) {
            VertexType vertex;

            memcpy(&vertex.Position, &positions.Data[i], sizeof(float32) * 3);
            memset(&vertex.Normal, 0, sizeof(vertex.Normal));

            vertices.Insert(vertex);
        }

        return vertices;
    }

    static FxStaticArray<VertexType> MakeCombinedVertexBuffer(FxStaticArray<float32> &positions, FxStaticArray<float32> &normals)
    {
        FxAssert((normals.Size == positions.Size));

        FxStaticArray<VertexType> vertices(positions.Size / 3);

        // Log::Info("Creating combined vertex buffer (s: %d)", vertices.Capacity);

        for (int i = 0; i < vertices.Capacity; i++) {
            // make a combined vertex + normal + other a
            VertexType vertex;

            memcpy(&vertex.Position, &positions.Data[i * 3], sizeof(float32) * 3);
            memcpy(&vertex.Normal, &normals.Data[i * 3], sizeof(float32) * 3);

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
        RvkFrameData *frame = RendererVulkan->GetFrame();

        vkCmdBindVertexBuffers(frame->CommandBuffer.CommandBuffer, 0, 1, &mVertexBuffer.Buffer, &offset);
        vkCmdBindIndexBuffer(frame->CommandBuffer.CommandBuffer, mIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

        frame->DescriptorSet.Bind(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

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
