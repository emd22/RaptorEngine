#include "Attachment.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

Target::Target(eImageFormat format, const Vec2u& size)
{
    Image.Format = format;
    Image.Size = size;

    if (size == Target::scFullScreen) {
        Image.Size = gRenderer->Swapchain.Extent;
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
    }
}

Target::Target(ImageFormat format, const Vec2u& size, VkImageUsageFlags usage, ImageAspectFlag aspect)
    : Usage(usage), Aspect(aspect)
{
    Image.Format = format;
    Image.Size = size;

    if (size == Target::scFullScreen) {
        Image.Size = gRenderer->Swapchain.Extent;
    }
}


void Target::CreateImage()
{
    if (bReuseImage) {
        return;
    }

    Image.Create(ImageType, Image.Size, Image.Format, VK_IMAGE_TILING_OPTIMAL, Usage, Aspect);
}


VkAttachmentDescription Target::BuildDescription() const
{
    Assert(Image.Format != ImageFormat::None);

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
    if (mbDescriptionsBuilt) {
        return mBuiltAttachmentDescriptions;
    }

    if (!mBuiltAttachmentDescriptions) {
        mBuiltAttachmentDescriptions.InitCapacity(mMaxTargets);
    }

    mBuiltAttachmentDescriptions.Clear();

    for (const Target& at : Targets) {
        mBuiltAttachmentDescriptions.Insert(at.BuildDescription());
    }

    mbDescriptionsBuilt = true;

    return mBuiltAttachmentDescriptions;
}


TargetList& TargetList::Add(const Target& attachment)
{
    CheckInited();
    Targets.Insert(attachment);

    mbImageViewsBuilt = false;

    return *this;
}

TargetList& TargetList::Add(const Target* attachment)
{
    AssertMsg(attachment != nullptr, "Attachment cannot be null!");
    return Add(*attachment);
}

void TargetList::CreateImages()
{
    for (Target& attachment : Targets) {
        attachment.CreateImage();
    }

    mbImagesCreated = true;
}

SizedArray<VkImageView>& TargetList::GetImageViews()
{
    // Return the list of views if it is already populated
    if (mbImageViewsBuilt) {
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

    mbImageViewsBuilt = true;

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
