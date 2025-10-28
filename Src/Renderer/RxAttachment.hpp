#pragma once

#include <vulkan/vulkan.h>

#include <Core/FxSizedArray.hpp>
#include <Core/FxSlice.hpp>

enum class RxLoadOp
{
    Clear = VK_ATTACHMENT_LOAD_OP_CLEAR,
    Load = VK_ATTACHMENT_LOAD_OP_LOAD,
    DontCare = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
};

enum class RxStoreOp
{
    None = VK_ATTACHMENT_STORE_OP_NONE,
    Store = VK_ATTACHMENT_STORE_OP_STORE,
    DontCare = VK_ATTACHMENT_STORE_OP_DONT_CARE,
};

struct RxAttachment
{
public:
    VkFormat Format = VK_FORMAT_B8G8R8A8_UNORM;
    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;

    RxLoadOp LoadOp = RxLoadOp::Clear;
    RxStoreOp StoreOp = RxStoreOp::Store;

    RxLoadOp StencilLoadOp = RxLoadOp::Clear;
    RxStoreOp StencilStoreOp = RxStoreOp::Store;

    VkImageLayout InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout FinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

public:
    VkAttachmentDescription Build() const
    {
        return VkAttachmentDescription {
            .format = Format,
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

    operator FxSlice<RxAttachment>() { return Attachments; }

    RxAttachmentList& Add(const RxAttachment& attachment)
    {
        CheckInited();
        Attachments.Insert(attachment);

        return *this;
    }

    FxSizedArray<VkAttachmentDescription>& GetBuiltAttachments()
    {
        if (mBuiltAttachments.Size == Attachments.Size) {
            return mBuiltAttachments;
        }

        if (!mBuiltAttachments) {
            mBuiltAttachments.InitCapacity(mMaxAttachments);
        }

        mBuiltAttachments.Clear();
        for (const RxAttachment& at : Attachments) {
            mBuiltAttachments.Insert(at.Build());
        }

        return mBuiltAttachments;
    }

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
    FxSizedArray<VkAttachmentDescription> mBuiltAttachments;
    uint32 mMaxAttachments = 10;
};
