#include "Target.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

Target::Target(eImageFormat format, const Vec2u& size)
{
    Image.Format = format;
    Image.Size = size;

    if (size == Target::scFullScreen) {
        Image.Size = gRenderer->Swapchain.Extent;
        bIsFullscreen = true;
    }
}

Target::Target(eImageFormat format, const Vec2u& size, eLoadOp load_op, eStoreOp store_op, VkImageLayout initial_layout,
               VkImageLayout final_layout)
    : LoadOp(load_op), StoreOp(store_op), InitialLayout(initial_layout), FinalLayout(final_layout)
{
    Image.Format = format;
    Image.Size = size;

    if (size == Target::scFullScreen) {
        Image.Size = gRenderer->Swapchain.Extent;
        bIsFullscreen = true;
    }
}

Target::Target(eImageFormat format, const Vec2u& size, VkImageUsageFlags usage, eImageAspectFlag aspect)
    : Usage(usage), Aspect(aspect)
{
    Image.Format = format;
    Image.Size = size;

    if (size == Target::scFullScreen) {
        Image.Size = gRenderer->Swapchain.Extent;
        bIsFullscreen = true;
    }
}


void Target::CreateImage()
{
    if (bReuseImage) {
        // If we have the target that the image is from set, we can pull the updated Image from there
        if (mpReferenceTarget != nullptr) {
            Image = mpReferenceTarget->GetImage();
        }

        return;
    }

    Image.Create(ImageType, Image.Size, Image.Format, VK_IMAGE_TILING_OPTIMAL, Usage, Aspect);
}


VkAttachmentDescription Target::BuildDescription() const
{
    Assert(Image.Format != eImageFormat::None);

    return VkAttachmentDescription {
        .format = ImageFormatUtil::ToUnderlying(Image.Format),
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


SizedArray<VkAttachmentDescription>& TargetList::GetDescriptions()
{
    // Return the descriptions if they are already built
    if ((mFlags & eTargetListFlags::DescriptionsBuilt) != 0) {
        return mBuiltAttachmentDescriptions;
    }

    if (!mBuiltAttachmentDescriptions) {
        mBuiltAttachmentDescriptions.InitCapacity(mMaxTargets);
    }

    mBuiltAttachmentDescriptions.Clear();

    for (const Target& at : Targets) {
        mBuiltAttachmentDescriptions.Insert(at.BuildDescription());
    }

    mFlags |= eTargetListFlags::DescriptionsBuilt;

    return mBuiltAttachmentDescriptions;
}


TargetList& TargetList::Add(const Target& attachment)
{
    CheckInited();
    Targets.Insert(attachment);

    mFlags &= ~(eTargetListFlags::ImageViewsBuilt);

    return *this;
}

TargetList& TargetList::Add(const Target* attachment)
{
    AssertMsg(attachment != nullptr, "Attachment cannot be null!");
    return Add(*attachment);
}

void TargetList::CreateImages()
{
    if ((mFlags & eTargetListFlags::ImagesCreated) != 0) {
        return;
    }

    Vec2u swapchain_size = gRenderer->Swapchain.Extent;

    for (Target& target : Targets) {
        if (target.bIsFullscreen) {
            // This size will be the size of the newly created image after running `CreateImage`.
            target.Image.Size = swapchain_size;
        }
        
        target.CreateImage();
    }

    mFlags |= eTargetListFlags::ImagesCreated;
}

SizedArray<VkImageView>& TargetList::GetImageViews()
{
    // Return the list of views if it is already populated
    if ((mFlags & eTargetListFlags::ImageViewsBuilt) != 0) {
        return mBuiltImageViews;
    }

    if (!mBuiltImageViews.IsInited()) {
        mBuiltImageViews.InitCapacity(Targets.Size);
    }

    mBuiltImageViews.Clear();

    for (Target& attachment : Targets) {
        // Check to ensure that the image (and therefore the view) is created.
        if (!attachment.Image.IsInited()) {
            continue;
        }

        mBuiltImageViews.Insert(attachment.Image.View);
    }

    mFlags |= eTargetListFlags::ImageViewsBuilt;

    return mBuiltImageViews;
}


bool TargetList::IsCompatible(const TargetList& other) const
{
    if (Targets.Size != other.Targets.Size) {
        return false;
    }

    for (uint32 index = 0; index < Targets.Size; index++) {
        const Target& t = Targets[index];
        const Target& other_t = other.Targets[index];

        const bool format_matches = t.Image.Format == other_t.Image.Format;

        if (!format_matches) {
            return false;
        }
    }

    return true;
}

} // namespace fx::renderer
