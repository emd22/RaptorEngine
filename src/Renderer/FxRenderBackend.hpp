#pragma once

#include <Math/Vec2.hpp>

#include <Renderer/FxWindow.hpp>

// TODO: make base versions of these components
#include <Renderer/Backend/Vulkan/Pipeline.hpp>
#include <Renderer/Backend/Vulkan/Swapchain.hpp>
#include <Renderer/Backend/Vulkan/FrameData.hpp>

#include <ThirdParty/vk_mem_alloc.h>

#include <deque>
#include <memory>

#include <vma/vk_mem_alloc.h>

using namespace vulkan;

enum class FrameResult
{
    Success,
    GraphicsOutOfDate,
    RenderError,
};

struct FxDeletionObject
{
    using FuncType = void (*)(FxDeletionObject *object);

    VkBuffer Buffer = VK_NULL_HANDLE;
    VmaAllocation Allocation = VK_NULL_HANDLE;

    uint32 DeletionFrameNumber = 0;
    FuncType Func;
};

class FxRenderBackend {
public:
    const uint32_t FramesInFlight = 2;

    virtual void Init(Vec2i window_size) = 0;
    virtual void Destroy() = 0;

    virtual void SelectWindow(std::shared_ptr<FxWindow> window) = 0;
    virtual std::shared_ptr<FxWindow> GetWindow() = 0;

    virtual FrameResult BeginFrame(GraphicsPipeline &pipeline, Mat4f &MVPMatrix) = 0;
    virtual void FinishFrame(GraphicsPipeline &pipeline) = 0;

    virtual void AddToDeletionQueue(FxDeletionObject::FuncType func)
    {
        Log::Info("Adding object to deletion queue at frame %d", mInternalFrameCounter);

        mDeletionQueue.push_back(FxDeletionObject {
            .DeletionFrameNumber = mInternalFrameCounter,
            .Func = func,
        });
    }

    virtual void AddGPUBufferToDeletionQueue(FxDeletionObject::FuncType func, VkBuffer buffer, VmaAllocation allocation)
    {
        Log::Info("Adding GPUBuffer to deletion queue at frame %d", mInternalFrameCounter);

        mDeletionQueue.push_back(FxDeletionObject {
            .DeletionFrameNumber = mInternalFrameCounter,
            .Func = func,
            .Allocation = allocation,
            .Buffer = buffer,
        });
    }

    uint32 GetFrameNumber() { return mFrameNumber; }

protected:
    virtual void ProcessDeletionQueue(bool ignore_frame_spacing = false)
    {
        if (mDeletionQueue.empty()) {
            return;
        }

        FxDeletionObject &object = mDeletionQueue.front();
        const uint32 FrameSpacing = 3;
        Log::Debug("Deleting object from deletion queue from frame %d", object.DeletionFrameNumber);

        const bool is_frame_spaced = (mInternalFrameCounter - object.DeletionFrameNumber) > FrameSpacing;

        if (ignore_frame_spacing || is_frame_spaced) {
            object.Func(&object);

            mDeletionQueue.pop_front();
        }
    }

public:
    bool Initialized = false;

protected:
    uint32 mFrameNumber = 0;
    uint32 mInternalFrameCounter = 0;

    std::deque<FxDeletionObject> mDeletionQueue;
};
