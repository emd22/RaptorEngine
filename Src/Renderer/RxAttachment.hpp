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
    static constexpr FxVec2u scFullScreen = FxVec2u(0U);

public:
    RxAttachment() = default;

    RxAttachment(RxImageFormat format, const FxVec2u& size);
    RxAttachment(RxImageFormat format, const FxVec2u& size, VkImageUsageFlags usage, RxImageAspectFlag aspect);
    RxAttachment(RxImageFormat format, const FxVec2u& size, RxLoadOp load_op, RxStoreOp store_op,
                 VkImageLayout initial_layout, VkImageLayout final_layout);


    VkAttachmentDescription BuildDescription() const;
    void CreateImage();

    RxImage& GetImage() { return Image; }
    VkImageView& GetImageView() { return Image.View; }

    bool IsDepth() const { return Aspect == RxImageAspectFlag::eDepth; }

public:
    RxImageType ImageType = RxImageType::e2d;

    VkImageUsageFlags Usage = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    RxImageAspectFlag Aspect = RxImageAspectFlag::eColor;

    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;

    RxLoadOp LoadOp = RxLoadOp::eClear;
    RxStoreOp StoreOp = RxStoreOp::eStore;

    RxLoadOp StencilLoadOp = RxLoadOp::eClear;
    RxStoreOp StencilStoreOp = RxStoreOp::eStore;

    VkImageLayout InitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageLayout FinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RxImage Image;
};

class RxAttachmentList
{
public:
    RxAttachmentList() = default;
    RxAttachmentList(uint32 max_attachments) : mMaxAttachments(max_attachments) {}
    RxAttachmentList(const RxAttachmentList& other) { Attachments.InitAsCopyOf(other.Attachments); }
    RxAttachmentList(RxAttachmentList&& other) noexcept { Attachments = std::move(other.Attachments); }


    static RxAttachmentList New() { return {}; }

    RxAttachmentList& Add(RxAttachment&& attachment);

    void CreateImages();
    FxSizedArray<VkAttachmentDescription>& GetAttachmentDescriptions();
    FxSizedArray<VkImageView>& GetImageViews();


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
    bool mbImageViewsBuilt : 1 = false;
    bool mbImagesCreated : 1 = false;

    FxSizedArray<VkAttachmentDescription> mBuiltAttachmentDescriptions;
    FxSizedArray<VkImageView> mBuiltImageViews;

    uint32 mMaxAttachments = 10;
};
