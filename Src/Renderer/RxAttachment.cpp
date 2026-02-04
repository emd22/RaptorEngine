#include "RxAttachment.hpp"

#include <FxEngine.hpp>
#include <Renderer/RxRenderBackend.hpp>

RxAttachment::RxAttachment(RxImageFormat format, const FxVec2u& size)
{
    Image.Format = format;
    Image.Size = size;

    if (size == RxAttachment::scFullScreen) {
        Image.Size = gRenderer->Swapchain.Extent;
    }
}

RxAttachment::RxAttachment(RxImageFormat format, const FxVec2u& size, RxLoadOp load_op, RxStoreOp store_op,
                           VkImageLayout initial_layout, VkImageLayout final_layout)
    : LoadOp(load_op), StoreOp(store_op), InitialLayout(initial_layout), FinalLayout(final_layout)
{
    Image.Format = format;
    Image.Size = size;

    if (size == RxAttachment::scFullScreen) {
        Image.Size = gRenderer->Swapchain.Extent;
    }
}

RxAttachment::RxAttachment(RxImageFormat format, const FxVec2u& size, VkImageUsageFlags usage, RxImageAspectFlag aspect)
    : Usage(usage), Aspect(aspect)
{
    Image.Format = format;
    Image.Size = size;

    if (size == RxAttachment::scFullScreen) {
        Image.Size = gRenderer->Swapchain.Extent;
    }
}


void RxAttachment::CreateImage()
{
    Image.Create(ImageType, Image.Size, Image.Format, VK_IMAGE_TILING_OPTIMAL, Usage, Aspect);
}


VkAttachmentDescription RxAttachment::BuildDescription() const
{
    FxAssert(Image.Format != RxImageFormat::eNone);

    return VkAttachmentDescription {
        .format = RxImageFormatUtil::ToUnderlying(Image.Format),
        .samples = Samples,

        .loadOp = static_cast<VkAttachmentLoadOp>(LoadOp),
        .storeOp = static_cast<VkAttachmentStoreOp>(StoreOp),

        .stencilLoadOp = static_cast<VkAttachmentLoadOp>(StencilLoadOp),
        .stencilStoreOp = static_cast<VkAttachmentStoreOp>(StencilStoreOp),

        .initialLayout = InitialLayout,
        .finalLayout = FinalLayout,
    };
}


///////////////////////////////////
// Attachment List Functions
///////////////////////////////////


FxSizedArray<VkAttachmentDescription>& RxAttachmentList::GetAttachmentDescriptions()
{
    // Return the descriptions if they are already built
    if (mbDescriptionsBuilt) {
        return mBuiltAttachmentDescriptions;
    }

    if (!mBuiltAttachmentDescriptions) {
        mBuiltAttachmentDescriptions.InitCapacity(mMaxAttachments);
    }

    mBuiltAttachmentDescriptions.Clear();

    for (const RxAttachment& at : Attachments) {
        mBuiltAttachmentDescriptions.Insert(at.BuildDescription());
    }

    mbDescriptionsBuilt = true;

    return mBuiltAttachmentDescriptions;
}


RxAttachmentList& RxAttachmentList::Add(const RxAttachment& attachment)
{
    CheckInited();
    Attachments.Insert(attachment);

    mbImageViewsBuilt = false;

    return *this;
}

void RxAttachmentList::CreateImages()
{
    for (RxAttachment& attachment : Attachments) {
        attachment.CreateImage();
    }

    mbImagesCreated = true;
}

FxSizedArray<VkImageView>& RxAttachmentList::GetImageViews()
{
    // Return the list of views if it is already populated
    if (mbImageViewsBuilt) {
        return mBuiltImageViews;
    }


    if (!mBuiltImageViews.IsInited()) {
        mBuiltImageViews.InitCapacity(Attachments.Size);
    }

    mBuiltImageViews.Clear();

    for (RxAttachment& attachment : Attachments) {
        // Check to ensure that the image (and therefore the view) is created.
        if (!attachment.Image.IsInited()) {
            continue;
        }

        mBuiltImageViews.Insert(attachment.Image.View);
    }

    mbImageViewsBuilt = true;

    return mBuiltImageViews;
}
