#include "Target.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

Target::Target(eImageFormat format, const Vec2u& size, bool is_fullscreen)
{
	Image.SetInfo(size, format, 0, 1);

	if (is_fullscreen) {
		Image.SetSize(gGraphics->Swapchain.Extent);
		bIsFullscreen = true;
	}
}

Target::Target(eImageFormat format, const Vec2u& size, bool is_fullscreen, eLoadOp load_op, eStoreOp store_op,
			   VkImageLayout initial_layout, VkImageLayout final_layout)
	: LoadOp(load_op), StoreOp(store_op), InitialLayout(initial_layout), FinalLayout(final_layout)
{
	Image.SetInfo(size, format, 0, 1);

	if (is_fullscreen) {
		Image.SetSize(gGraphics->Swapchain.Extent);
		bIsFullscreen = true;
	}
}

Target::Target(eImageFormat format, const Vec2u& size, bool is_fullscreen, VkImageUsageFlags usage,
			   eImageAspectFlag aspect)
	: Usage(usage), Aspect(aspect)
{
	Image.SetInfo(size, format, 0, 1);

	if (is_fullscreen) {
		Image.SetSize(gGraphics->Swapchain.Extent);
		bIsFullscreen = true;
	}
}


void Target::CreateImage()
{
	if (bImageIsReference) {
		// If we have the target that the image is from set, we can pull the updated Image from there
		if (mpReferenceTarget != nullptr) {
			Image = mpReferenceTarget->GetImage();
		}

		return;
	}

	Image.Create(ImageType, Image.GetSize(), 1, Image.GetFormat(), VK_IMAGE_TILING_OPTIMAL, Usage, Aspect,
				 eImageCreateFlags::IsTarget);
}


VkAttachmentDescription Target::BuildDescription() const
{
	Assert(Image.GetFormat() != eImageFormat::None);

	const RxAttachmentInfo info = {
		.format = ImageFormatUtil::ToUnderlying(Image.GetFormat()),
		.samples = static_cast<uint32>(Samples),
		.load_op = static_cast<int32>(LoadOp),
		.store_op = static_cast<int32>(StoreOp),
		.stencil_load_op = static_cast<int32>(StencilLoadOp),
		.stencil_store_op = static_cast<int32>(StencilStoreOp),
		.initial_layout = static_cast<int32>(InitialLayout),
		.final_layout = static_cast<int32>(FinalLayout),
	};

	VkAttachmentDescription description;

	rx_attachment_description(&info, &description);

	return description;
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

void TargetList::CreateImages(const Vec2u& size)
{
	if ((mFlags & eTargetListFlags::ImagesCreated) != 0) {
		return;
	}

	for (Target& target : Targets) {
		if (!target.bImageIsReference) {
			target.Image.SetSize(size);
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

		mBuiltImageViews.Insert(attachment.Image.GetView());
	}

	mFlags |= eTargetListFlags::ImageViewsBuilt;

	return mBuiltImageViews;
}


bool TargetList::IsCompatible(const TargetList& other) const
{
	std::vector<uint16> formats;
	std::vector<uint16> other_formats;

	for (const Target& target : Targets) {
		formats.push_back(static_cast<uint16>(target.Image.GetFormat()));
	}

	for (const Target& target : other.Targets) {
		other_formats.push_back(static_cast<uint16>(target.Image.GetFormat()));
	}

	return rx_formats_compatible(formats.data(), formats.size(), other_formats.data(), other_formats.size()) != 0;
}

} // namespace fx::renderer
