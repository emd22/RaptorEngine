#pragma once

#include <Math/Vec2.hpp>

#include <Renderer/FxWindow.hpp>

// TODO: make base versions of these components
#include <Renderer/Backend/Vulkan/RvkPipeline.hpp>
#include <Renderer/Backend/Vulkan/RvkSwapchain.hpp>

#include <ThirdParty/vk_mem_alloc.h>
#include <Renderer/Backend/Vulkan/FxDeletionObject.hpp>

#include <deque>
#include <memory>

#include <vma/vk_mem_alloc.h>

// using namespace vulkan;

enum class FrameResult
{
    Success,
    GraphicsOutOfDate,
    RenderError,
};

class FxRenderBackend {
    const uint32 DeletionFrameSpacing = 3;

public:
    virtual void Init(Vec2u window_size) = 0;
    virtual void Destroy() = 0;

    virtual void SelectWindow(std::shared_ptr<FxWindow> window) = 0;
    virtual std::shared_ptr<FxWindow> GetWindow() = 0;

    virtual FrameResult BeginFrame(vulkan::RvkGraphicsPipeline &pipeline, Mat4f &MVPMatrix) = 0;
    virtual void FinishFrame(vulkan::RvkGraphicsPipeline &pipeline) = 0;

    virtual void AddToDeletionQueue(FxDeletionObject::FuncType func)
    {
        Log::Info("Adding object to deletion queue at frame %d", mInternalFrameCounter);

        mDeletionQueue.push_back(FxDeletionObject {
            .DeletionFrameNumber = mInternalFrameCounter,
            .Func = func,
        });
    }

    virtual void AddGpuBufferToDeletionQueue(VkBuffer buffer, VmaAllocation allocation)
    {
        // Log::Info("Adding GPUBuffer to deletion queue at frame %d", mInternalFrameCounter);
        if (mInDeletionQueue) {
            mInDeletionQueue.wait(false);
        }

        mInDeletionQueue.store(true);

        mDeletionQueue.push_back(FxDeletionObject {
            .DeletionFrameNumber = mInternalFrameCounter + DeletionFrameSpacing,
            .IsGpuBuffer = true,
            .Allocation = allocation,
            .Buffer = buffer,
        });

        mInDeletionQueue.store(false);
    }

    uint32 GetFrameNumber() { return mFrameNumber; }

protected:
    // virtual

public:
    bool Initialized = false;

protected:
    uint32 mFrameNumber = 0;
    uint32 mInternalFrameCounter = 0;

    std::atomic_bool mInDeletionQueue = false;
    std::deque<FxDeletionObject> mDeletionQueue;
};
