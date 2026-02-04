#include "RxRenderStage.hpp"

#include <FxEngine.hpp>
#include <Renderer/RxRenderBackend.hpp>


void RxRenderStage::BuildInputDescriptors(RxDescriptorSet* out)
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

            const VkWriteDescriptorSet image_write {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = out->Get(),
                .dstBinding = target.BindIndex,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = image_infos.Insert(image_info),
            };

            write_infos.Insert(image_write);
        }
        else if (target.pBuffer) {
            const VkDescriptorBufferInfo buffer_info {
                .buffer = gRenderer->Uniforms.GetGpuBuffer().Buffer,
                .offset = target.BufferOffset,
                .range = target.BufferRange,
            };

            const VkWriteDescriptorSet buffer_write {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = out->Get(),
                .dstBinding = target.BindIndex,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .pImageInfo = nullptr,
                .pBufferInfo = buffer_infos.Insert(buffer_info),
            };

            write_infos.Insert(buffer_write);
        }
    }

    vkUpdateDescriptorSets(gRenderer->GetDevice()->Device, write_infos.Size, write_infos.pData, 0, nullptr);
}

RxAttachment* RxRenderStage::GetTarget(RxImageFormat format, int sub_index)
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

void RxRenderStage::AddInputTarget(uint32 bind_index, RxAttachment* target, RxSampler* sampler)
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

void RxRenderStage::AddInputBuffer(uint32 bind_index, RxRawGpuBuffer* buffer, uint32 offset, uint32 range)
{
    if (!mInputTargets.IsInited()) {
        mInputTargets.InitCapacity(scMaxInputAttachments);
    }

    FxAssertMsg(buffer != nullptr, "Input buffer cannot be null!");

    InputTarget input_buffer {
        .BindIndex = bind_index,
        .pTarget = nullptr,
        .pSampler = nullptr,
        .pBuffer = buffer,
        .BufferOffset = offset,
        .BufferRange = range,
    };

    mInputTargets.Insert(input_buffer);
}

void RxRenderStage::BuildRenderStage()
{
    if (mbIsBuilt) {
        return;
    }

    if (mbIsFinalStage) {
        AddPresentTarget();
    }

    // If there are no clear values already defined, generate them
    if (ClearValues.Size == 0) {
        MakeClearValues();
    }

    mOutputTargets.CreateImages();

    mRenderPass.Create(mOutputTargets, mSize);

    if (mbIsFinalStage) {
        CreateFinalStageFramebuffers();
    }
    else {
        mFramebuffer.Create(mOutputTargets.GetImageViews(), mRenderPass, mSize);
    }

    mbIsBuilt = true;
}

void RxRenderStage::Begin(RxCommandBuffer& cmd, RxPipeline& pipeline)
{
    FxAssert(mbIsBuilt);

    VkFramebuffer framebuffer;

    if (mbIsFinalStage) {
        framebuffer = mFinalStageFramebuffers[gRenderer->GetImageIndex()].Get();
    }
    else {
        framebuffer = mFramebuffer.Get();
    }

    mRenderPass.Begin(&cmd, framebuffer, ClearValues);
    pipeline.Bind(cmd);
}

void RxRenderStage::AddTarget(RxImageFormat format, const FxVec2u& size, VkImageUsageFlags usage,
                              RxImageAspectFlag aspect)
{
    mOutputTargets.Add(RxAttachment(format, size, usage, aspect));
}


void RxRenderStage::MakeClearValues()
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


void RxRenderStage::CreateFinalStageFramebuffers()
{
    FxSizedArray<RxImage>& final_images = gRenderer->Swapchain.OutputImages;

    mFinalStageFramebuffers.InitSize(final_images.Size);

    FxSizedArray<VkImageView> image_views;
    image_views.InitSize(1);

    for (uint32 i = 0; i < final_images.Size; i++) {
        image_views[0] = final_images[i].View;
        mFinalStageFramebuffers[i].Create(image_views, mRenderPass, gRenderer->Swapchain.Extent);
    }
}


void RxRenderStage::MarkFinalStage()
{
    FxAssertMsg(mbIsBuilt == false, "Cannot mark final -- Render stage was already built!");

    mbIsFinalStage = true;
}


void RxRenderStage::AddPresentTarget()
{
    mOutputTargets.Add(RxAttachment(gRenderer->Swapchain.Surface.Format, RxAttachment::scFullScreen,
                                    RxLoadOp::eDontCare, RxStoreOp::eStore, VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));
}
