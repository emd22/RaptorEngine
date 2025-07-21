#include <vulkan/vulkan.h>
#include <stdexcept>

#include <Renderer/Renderer.hpp>

#include "RvkDevice.hpp"
#include "RvkRenderPass.hpp"

#include "RvkSwapchain.hpp"

#include <Core/FxPanic.hpp>

FX_SET_MODULE_NAME("RvkRenderPass")

void RvkRenderPass::Create2(const FxSlice<VkAttachmentDescription>& attachments)
{
    mDevice = Renderer->GetDevice();

    FxSizedArray<VkAttachmentReference> color_refs(attachments.Size);

    bool has_depth_attachment = false;
    VkAttachmentReference depth_attachment_ref{};

    for (int i = 0; i < attachments.Size; i++) {
        VkAttachmentDescription& attachment = attachments[i];

        if (RvkUtil::IsFormatDepth(attachment.format)) {
            has_depth_attachment = true;

            depth_attachment_ref.attachment = i;
            depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        else {
            VkAttachmentReference* ref = color_refs.Insert();

            ref->attachment = i;
            ref->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
    }

    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = static_cast<uint32>(color_refs.Size),
        .pColorAttachments = color_refs.Data,
        .pDepthStencilAttachment = has_depth_attachment ? &depth_attachment_ref : nullptr,
        .pResolveAttachments = nullptr,
    };

    VkSubpassDependency subpass_dependencies[] = {
        VkSubpassDependency {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,

            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                          | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,

            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                           | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,

            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,

            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        },
        VkSubpassDependency {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,

            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                          | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,

            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,

            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                           | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,

            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        }
    };

    // VkSubpassDependency subpass_dependencies[] = {
    //     VkSubpassDependency {
    //         .srcSubpass = 0,
    //         .dstSubpass = VK_SUBPASS_EXTERNAL,

    //         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    //                       | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
    //                       | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,

    //         .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,

    //         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    //                        | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
    //                        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
    //                        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,

    //         .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,

    //         .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    //     },
    //     VkSubpassDependency {
    //         .srcSubpass = VK_SUBPASS_EXTERNAL,
    //         .dstSubpass = 0,

    //         .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,

    //         .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    //                       | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
    //                       | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,

    //         .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,

    //         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    //                        | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
    //                        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
    //                        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,

    //         .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    //     }
    // };

    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32>(attachments.Size),
        .pAttachments = attachments.Ptr,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = sizeof(subpass_dependencies) / sizeof(subpass_dependencies[0]),
        .pDependencies = subpass_dependencies,
    };

    const VkResult status = vkCreateRenderPass(mDevice->Device, &create_info, nullptr, &RenderPass);

    if (status != VK_SUCCESS) {
        FxModulePanic("Failed to create render pass", status);
    }

}

void RvkRenderPass::Begin(RvkCommandBuffer* cmd, VkFramebuffer framebuffer, const FxSlice<VkClearValue>& clear_values)
{
    CommandBuffer = cmd;

    if (RenderPass == nullptr) {
        FxModulePanic("Render pass has not been previously created", 0);
    }

    const auto extent = Renderer->Swapchain.Extent;

    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = RenderPass,
        .framebuffer = framebuffer,
        .renderArea.offset = {0, 0},
        .renderArea.extent = {(uint32)extent.Width(), (uint32)extent.Height()},
        .clearValueCount = clear_values.Size,
        .pClearValues = clear_values.Ptr,
    };

    vkCmdBeginRenderPass(cmd->CommandBuffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
}

void RvkRenderPass::End()
{
    FxAssert(CommandBuffer != nullptr);

    vkCmdEndRenderPass(CommandBuffer->CommandBuffer);
}

void RvkRenderPass::Destroy()
{
    if (RenderPass != nullptr) {
        vkDestroyRenderPass(mDevice->Device, RenderPass, nullptr);
    }
    RenderPass = nullptr;
}
