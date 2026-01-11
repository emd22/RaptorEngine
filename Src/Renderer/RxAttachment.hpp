#pragma once

#include "Backend/RxImage.hpp"

#include <vulkan/vulkan.h>

#include <Core/FxSizedArray.hpp>
#include <Core/FxSlice.hpp>

enum class RxLoadOp
{
    eClear = VK_ATTACHMENT_LOAD_OP_CLEAR,
    eLoad = VK_ATTACHMENT_LOAD_OP_LOAD,
    eDontCare = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
};

enum class RxStoreOp
{
    eNone = VK_ATTACHMENT_STORE_OP_NONE,
    eStore = VK_ATTACHMENT_STORE_OP_STORE,
    eDontCare = VK_ATTACHMENT_STORE_OP_DONT_CARE,
};


struct RxAttachment
{
public:
    RxImageType ImageType = RxImageType::e2d;

    VkImageUsageFlags Usage = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    RxImageFormat Format = RxImageFormat::eNone;
    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;

    RxLoadOp LoadOp = RxLoadOp::eClear;
    RxStoreOp StoreOp = RxStoreOp::eStore;

    RxLoadOp StencilLoadOp = RxLoadOp::eClear;
    RxStoreOp StencilStoreOp = RxStoreOp::eStore;

    VkImageLayout InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout FinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

public:
    VkAttachmentDescription BuildDescription() const
    {
        FxAssert(Format != RxImageFormat::eNone);

        return VkAttachmentDescription {
            .format = RxImageFormatUtil::ToUnderlying(Format),
            .samples = Samples,

            .loadOp = static_cast<VkAttachmentLoadOp>(LoadOp),
            .storeOp = static_cast<VkAttachmentStoreOp>(StoreOp),

            .stencilLoadOp = static_cast<VkAttachmentLoadOp>(StencilLoadOp),
            .stencilStoreOp = static_cast<VkAttachmentStoreOp>(StencilStoreOp),

            .initialLayout = InitialLayout,
            .finalLayout = FinalLayout,
        };
    }
};

class RxAttachmentList
{
public:
    RxAttachmentList() = default;
    RxAttachmentList(uint32 max_attachments) : mMaxAttachments(max_attachments) {}
    RxAttachmentList(const RxAttachmentList& other) { Attachments.InitAsCopyOf(other.Attachments); }
    RxAttachmentList(RxAttachmentList&& other) { Attachments = std::move(other.Attachments); }

    static RxAttachmentList New() { return {}; }

    RxAttachmentList& Add(const RxAttachment& attachment);

    FxSizedArray<VkAttachmentDescription>& GetAttachmentDescriptions();


private:
    FX_FORCE_INLINE void CheckInited()
    {
        if (!Attachments) {
            Attachments.InitCapacity(mMaxAttachments);
        }
    }


public:
    FxSizedArray<RxAttachment> Attachments;


private:
    bool mbDescriptionsBuilt : 1 = false;
    bool mbImagesBuilt : 1 = false;

    FxSizedArray<VkAttachmentDescription> mBuiltAttachmentDescriptions;
    uint32 mMaxAttachments = 10;
};
