#pragma once

#include "Renderer/Backend/Vulkan/FrameData.hpp"
#include "Renderer/Renderer.hpp"
#include "vulkan/vulkan_core.h"
#include <Renderer/Backend/Vulkan/FxGpuBuffer.hpp>
#include <atomic>

// TEMP
using namespace vulkan;



class FxMesh
{
public:
    FxMesh() = default;

    using VertexType = FxVertex<FxVertexPosition | FxVertexNormal>;

    FxMesh(StaticArray<VertexType> &vertices)
    {
        SetVertices(vertices);
    }

    FxMesh(StaticArray<VertexType> &vertices, StaticArray<uint32> &indices)
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

    void CreateFromData(StaticArray<VertexType> &vertices, StaticArray<uint32> &indices)
    {
        SetVertices(vertices);
        SetIndices(indices);
    }

    void SetVertices(StaticArray<VertexType> &vertices)
    {
        mVertexBuffer.Create(FxBufferUsageType::Vertices, vertices);
    }

    void SetIndices(StaticArray<uint32> &indices)
    {
        mIndexBuffer.Create(FxBufferUsageType::Indices, indices);
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

    static StaticArray<VertexType> MakeCombinedVertexBuffer(StaticArray<float32> &positions)
    {
        StaticArray<VertexType> vertices(positions.Size / 3);

        Log::Info("Creating combined vertex buffer (Contains:Position) (s: %d)", vertices.Capacity);

        for (int i = 0; i < vertices.Capacity; i++) {
            VertexType vertex;

            memcpy(&vertex.Position, &positions.Data[i], sizeof(float32) * 3);
            memset(&vertex.Normal, 0, sizeof(vertex.Normal));

            vertices.Insert(vertex);
        }

        return vertices;
    }

    static StaticArray<VertexType> MakeCombinedVertexBuffer(StaticArray<float32> &positions, StaticArray<float32> &normals)
    {
        FxAssert((normals.Size == positions.Size));

        StaticArray<VertexType> vertices(positions.Size / 3);

        Log::Info("Creating combined vertex buffer (s: %d)", vertices.Capacity);

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

    FxGpuBuffer<VertexType> &GetVertexBuffer() { return mVertexBuffer; }
    FxGpuBuffer<uint32> &GetIndexBuffer() { return mIndexBuffer; }

    void Render()
    {
        const VkDeviceSize offset = 0;
        const FrameData *frame = RendererVulkan->GetFrame();

        vkCmdBindVertexBuffers(frame->CommandBuffer.CommandBuffer, 0, 1, &mVertexBuffer.Buffer, &offset);
        vkCmdBindIndexBuffer(frame->CommandBuffer.CommandBuffer, mIndexBuffer.Buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(frame->CommandBuffer.CommandBuffer, mIndexBuffer.Size, 1, 0, 0, 0);
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
    FxGpuBuffer<VertexType> mVertexBuffer;
    FxGpuBuffer<uint32> mIndexBuffer;
};
