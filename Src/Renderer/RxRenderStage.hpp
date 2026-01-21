#pragma once

#include "Backend/Fwd/Rx_Fwd_GetDevice.hpp"
#include "Backend/RxFramebuffer.hpp"
#include "Backend/RxImage.hpp"
#include "Backend/RxRenderPass.hpp"
#include "Backend/RxSynchro.hpp"

#include <Core/FxSizedArray.hpp>

template <uint32 TNumTargets>
class RxRenderStage
{
    static constexpr uint32 scMaxInputAttachments = 6;
    static constexpr uint32 scMaxInputBuffers = 2;

    struct InputTarget
    {
        uint32 BindIndex = 0;
        RxAttachment* pTarget = nullptr;
        RxSampler* pSampler = nullptr;
        RxGpuBuffer* pBuffer = nullptr;

        uint32 BufferOffset = 0;
        uint32 BufferRange = 0;
    };

public:
    RxRenderStage() = default;

    void Create(const FxVec2u& size) { mSize = size; }

    void AddWaitSemaphore() {}

    void AddTarget(RxImageFormat format, const FxVec2u& size, VkImageUsageFlags usage, RxImageAspectFlag aspect)
    {
        mOutputTargets.Add(RxAttachment(format, size, usage, aspect));
    }


    RxAttachmentList& GetTargets() { return mOutputTargets; }

    RxAttachment* GetTarget(RxImageFormat format, int sub_index = 0)
    {
        for (RxAttachment& attachment : mOutputTargets.Attachments) {
            if (attachment.Image.Format == format) {
                if ((sub_index--) > 0) {
                    continue;
                }

                return &attachment;
            }
        }

        return nullptr;
    }

    void AddInputBuffer(uint32 bind_index, RxGpuBuffer* buffer, uint32 offset, uint32 range)
    { 
        if (!mInputTargets.IsInited()) {
            mInputTargets.InitCapacity(scMaxInputAttachments);
        }

        FxAssertMsg(buffer != nullptr, "Input target cannot be null!");

        InputTarget input_target {
            .BindIndex = bind_index,
            .pTarget = nullptr,
            .pSampler = nullptr,
            .pBuffer = buffer,
            .BufferOffset = offset,
            .BufferRange = range
        };

        mInputTargets.Insert(input_target);
    }

    void AddInputTarget(uint32 bind_index, RxAttachment* target, RxSampler* sampler)
    { 
        if (!mInputTargets.IsInited()) {
            mInputTargets.InitCapacity(scMaxInputAttachments);
        }

        FxAssertMsg(target != nullptr, "Input target cannot be null!");

        InputTarget input_target {
            .BindIndex = bind_index,
            .pTarget = target,
            .pSampler = sampler,
            .pBuffer = nullptr,
        };

        mInputTargets.Insert(input_target);
    }

    void BuildInputDescriptors(RxDescriptorSet* out)
    {
        FxStackArray<VkDescriptorImageInfo, scMaxInputAttachments> image_infos;
        FxStackArray<VkDescriptorBufferInfo, scMaxInputBuffers> buffer_infos;
        FxStackArray<VkWriteDescriptorSet, scMaxInputAttachments + scMaxInputBuffers> write_infos;

        for (const InputTarget& target : mInputTargets) {
            if (target.pTarget) {
                VkDescriptorImageInfo image_info {
                    .sampler = target.pSampler->Sampler,
                    .imageView = target.pTarget->Image.View,
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,    
                };

                const VkWriteDescriptorSet write {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = out->Get(),
                    .dstBinding = target.BindIndex,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = image_infos.Insert(image_info),
                };

                write_infos.Insert(write);
            }
            else if (target.pBuffer) {
                const VkDescriptorBufferInfo buffer_info { 
                    .buffer = gRenderer->Uniforms.GetGpuBuffer().Buffer,
                    .offset = target.BufferOffset,
                    .range = target.BufferRange, 
                };

                const VkWriteDescriptorSet uniform_write {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = DescriptorSet.Set,
                    .dstBinding = binding_index,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    .pImageInfo = nullptr,
                    .pBufferInfo = buffer_infos.Insert(buffer_info),
                };

                write_infos.Insert(write);
            }
        }

        vkUpdateDescriptorSets(Rx_Fwd_GetDevice()->Device, write_infos.Size, write_infos.pData, 0, nullptr);
    }

    RxRenderPass& GetRenderPass() { return mRenderPass; }

    /**
     * @brief Builds the Vulkan objects for the render stage. This is deferred until the user requests a renderpass or
     * attachment. This is to reduce unused render stages, allow changes before the stage is built, etc.
     */
    void BuildRenderStage()
    {
        if (mbIsBuilt) {
            return;
        }

        FxAssert(mOutputTargets.Attachments.Size == TNumTargets);

        // If there are no clear values already defined, generate them
        if (ClearValues.Size == 0) {
            MakeClearValues();
        }

        mOutputTargets.CreateImages();

        mRenderPass.Create(mOutputTargets, mSize);
        mFramebuffer.Create(mOutputTargets.GetImageViews(), mRenderPass, mSize);

        mbIsBuilt = true;
    }

    void Begin(RxCommandBuffer& cmd, RxPipeline& pipeline)
    {
        FxAssert(mbIsBuilt);

        mRenderPass.Begin(&cmd, mFramebuffer.Get(), ClearValues);
        pipeline.Bind(cmd);
    }

    void End() { mRenderPass.End(); }

private:
    void MakeClearValues()
    {
        for (const RxAttachment& attachment : mOutputTargets.Attachments) {
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
    FxSizedArray<InputTarget> mInputTargets;

    RxFramebuffer mFramebuffer;
    RxRenderPass mRenderPass;

    bool mbIsBuilt : 1 = false;

    FxVec2u mSize = FxVec2u::sZero;
};
