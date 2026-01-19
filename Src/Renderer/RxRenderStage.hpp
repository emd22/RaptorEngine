#pragma once

#include "Backend/RxFramebuffer.hpp"
#include "Backend/RxImage.hpp"
#include "Backend/RxRenderPass.hpp"

#include <Core/FxSizedArray.hpp>

template <uint32 TNumTargets>
class RxRenderStage
{
    struct TargetOptions
    {
    };

public:
    RxRenderStage() = default;

    void Create(const FxVec2u& size) { mSize = size; }

    void AddTarget(RxImageFormat format, const FxVec2u& size, VkImageUsageFlags usage, RxImageAspectFlag aspect)
    {
         mOutputTargets.Add(RxAttachment(format, size, usage, aspect));
    }

    
    RxAttachmentList& GetTargets()
    {
        return mOutputTargets;
    }

    RxAttachment* GetTarget(RxImageFormat format, int skip = 0)
    {
        for (RxAttachment& attachment : mOutputTargets) {
            if (attachment.Image.Format == format) {
                if ((skip--) > 0) {
                    continue;
                }

                return &attachment;
            }
        }

        return nullptr;
    }

    void Submit(const FxSlice<RxCommandBuffer>& cmd_buffers, const FxSlice<RxSemaphore>& wait_semaphores, const FxSlice<RxSemaphore>& signal_semaphores)
    {
        RxFrameData* frame = gRenderer->GetFrame();

        const VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT }

        
        // Get the underlying command buffers
        FxSizedArray<VkCommandBuffer> cmd_buffers_vk(cmd_buffers.Size);
        for (const RxCommandBuffer& cmd : cmd_buffers) {
            cmd_buffers_vk.Insert(cmd.Get());
        }

        FxSizedArray<VkSemaphore> wait_semaphores_vk(wait_semaphores.Size);
        for (const RxSemaphore& sem : wait_semaphores) {
            wait_semaphores_vk.Insert(sem.Get());
        }

        
        FxSizedArray<VkSemaphore> signal_semaphores_vk(signal_semaphores.Size);
        for (const RxSemaphore& sem : signal_semaphores) {
            signal_semaphores_vk.Insert(sem.Get());
        }



        const VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = wait_semaphores_vk.Size,
            .pWaitSemaphores = wait_semaphores_vk.pData,
            .pWaitDstStageMask = wait_stages,

            .commandBufferCount = cmd_buffers_vk.Size,
            .pCommandBuffers = cmd_buffers_vk.pData,

            .signalSemaphoreCount = signal_semaphores_vk.Size,
            .pSignalSemaphores = &frame->OffscreenSem.Semaphore,
        };

        VkTry(vkQueueSubmit(gRenderer->GetDevice()->GraphicsQueue, 1, &submit_info, VK_NULL_HANDLE),
              "Error submitting draw buffer");
    }

    RxRenderPass& GetRenderPass()
    { 
        return mRenderPass;
    }

    /**
     * @brief Builds the Vulkan objects for the render stage. This is deferred until the user requests a renderpass or
     * attachment. This is to reduce unused render stages, allow changes before the stage is built, etc.
     */
    void BuildRenderStage()
    {
        if (mbIsBuilt) {
            return;
        }

        FxAssert(mOutputTargets.Size == TNumTargets);

        // If there are no clear values already defined, generate them
        if (ClearValues.Size == 0) {
            MakeClearValues();
        }

        mOutputTargets.CreateImages();

        mRenderPass.Create(mOutputTargets, mSize);
        mFramebuffer.Create(mOutputTargets.GetImageViews(), mRenderPass, mSize);

        mbIsBuilt = true;
    }

    void Begin(RxCommandBuffer& cmd, const RxPipeline& pipeline) 
    { 
        FxAssert(mbIsBuilt);

        mRenderPass->Begin(&cmd, mFramebuffer.Get(), ClearValues);
        pipeline.Bind(cmd);
    }

    void End() { mRenderPass->End(); }

private:
    void MakeClearValues()
    { 
        for (const RxAttachment& attachment : mOutputTargets) {
            if (attachment.Aspect == RxImageAspectFlag::eDepth) {
                ClearValues.Insert(VkClearValue { .depthStencil = { 0.0f, 0U } });
            }
            else if (attachment.Aspect == RxImageAspectFlag::eColor) {
                ClearValues.Insert(VkClearValue { .color = { { 0.0f, 0.0f, 0.0f, 0.0f } } });
            }
        }
    }

public:
    FxStackArray<VkClearValue, TNumTargets> ClearValues;

private: 
    RxAttachmentList mOutputTargets;

    RxFramebuffer mFramebuffer;
    RxRenderPass mRenderPass;

    bool mbIsBuilt : 1 = false;

    FxVec2u mSize = FxVec2u::sZero;
};
