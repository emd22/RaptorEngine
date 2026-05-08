#pragma once

#include <vulkan/vulkan.h>

#include <Color.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>

namespace fx::renderer {

union PipelineBlendFactor
{
    struct
    {
        VkBlendFactor Src : 8;
        VkBlendFactor Dst : 8;
    } Ops;

    uint16 Value;
};

union PipelineBlendOp
{
    struct
    {
        VkBlendOp Alpha : 8;
        VkBlendOp Color : 8;
    } Ops;

    uint16 Value;
};


struct BlendAttachment
{
    bool Enabled = false;
    eColorComponent Mask = eColorComponent::RGBA;
    PipelineBlendOp BlendOp = { .Ops = { .Alpha = VK_BLEND_OP_ADD, .Color = VK_BLEND_OP_ADD } };

    PipelineBlendFactor AlphaBlend = { .Ops = { .Src = VK_BLEND_FACTOR_ZERO, .Dst = VK_BLEND_FACTOR_ZERO } };
    PipelineBlendFactor ColorBlend = { .Ops = { .Src = VK_BLEND_FACTOR_ZERO, .Dst = VK_BLEND_FACTOR_ZERO } };

    uint16 TargetIndex = 0;
};

class BlendAttachmentList
{
    static constexpr uint32 scMinAttachmentCount = 10;

public:
    void AddAttachment(uint32 target_index, const BlendAttachment& attachment)
    {
        if (!mBlendAttachments.IsInited()) {
            mBlendAttachments.InitCapacity(scMinAttachmentCount);
        }

        BlendAttachment& at = mBlendAttachments.Insert(attachment);
        at.TargetIndex = target_index;
    }

    SizedArray<VkPipelineColorBlendAttachmentState> GetVkAttachments(uint32 count)
    {
        SizedArray<VkPipelineColorBlendAttachmentState> vk_blend_attachments;

        vk_blend_attachments.InitSize(count);

        for (uint32 i = 0; i < count; i++) {
            BlendAttachment am {};

            vk_blend_attachments[i] = (VkPipelineColorBlendAttachmentState {
                .blendEnable = am.Enabled ? 1U : 0U,
                .srcColorBlendFactor = am.ColorBlend.Ops.Src,
                .dstColorBlendFactor = am.ColorBlend.Ops.Dst,
                .colorBlendOp = am.BlendOp.Ops.Color,
                .srcAlphaBlendFactor = am.AlphaBlend.Ops.Src,
                .dstAlphaBlendFactor = am.AlphaBlend.Ops.Dst,
                .alphaBlendOp = am.BlendOp.Ops.Alpha,
                .colorWriteMask = static_cast<VkColorComponentFlags>(am.Mask),
            });
        }

        for (const BlendAttachment& am : mBlendAttachments) {
            vk_blend_attachments[am.TargetIndex] = (VkPipelineColorBlendAttachmentState {
                .blendEnable = am.Enabled ? 1U : 0U,
                .srcColorBlendFactor = am.ColorBlend.Ops.Src,
                .dstColorBlendFactor = am.ColorBlend.Ops.Dst,
                .colorBlendOp = am.BlendOp.Ops.Color,
                .srcAlphaBlendFactor = am.AlphaBlend.Ops.Src,
                .dstAlphaBlendFactor = am.AlphaBlend.Ops.Dst,
                .alphaBlendOp = am.BlendOp.Ops.Alpha,
                .colorWriteMask = static_cast<VkColorComponentFlags>(am.Mask),
            });
        }

        return vk_blend_attachments;
    }

    const SizedArray<BlendAttachment>& GetAttachments() const { return mBlendAttachments; }

    void Clear() { mBlendAttachments.Clear(); }

private:
    SizedArray<BlendAttachment> mBlendAttachments {};
};

} // namespace fx::renderer
