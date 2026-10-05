#include "RenderStage.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

static Vec2u GetRealTargetSize(const Vec2u& size)
{
	if (size == Target::scFullScreen) {
		return gGraphics->Swapchain.Extent;
	}
	return size;
}

RenderStage::RenderStage() { mpStage = rx_render_stage_new(); }

RenderStage::~RenderStage()
{
	GraphicsBackend* graphics = gGraphics;

	const bool can_free = (graphics != nullptr) && (graphics->GetDevice()->GetRustDevice() != nullptr) &&
						  (graphics->GpuAllocator != nullptr);

	rx_render_stage_destroy(mpStage, can_free ? graphics->GetDevice()->GetRustDevice() : nullptr,
							can_free ? graphics->GpuAllocator : nullptr);
}

void RenderStage::Create(const char* name, const Vec2u& size, eSizeDivisor size_divisor)
{
	pcName = name;

	if (size == Target::scFullScreen) {
		mbIsFullscreen = true;
	}

	mSize = GetRealTargetSize(size);
	mSizeDivisor = static_cast<uint32>(size_divisor);
}

TargetRef RenderStage::GetTarget(eImageFormat format, int32 sub_index) const
{
	return TargetRef(mpStage, rx_render_stage_find_target(mpStage, static_cast<uint16>(format), sub_index));
}

std::vector<uint16> RenderStage::GetColorTargetFormats() const
{
	std::vector<uint16> formats(rx_render_stage_color_target_formats(mpStage, nullptr, 0));

	rx_render_stage_color_target_formats(mpStage, formats.data(), formats.size());

	return formats;
}

Slice<VkAttachmentDescription> RenderStage::GetDescriptions()
{
	const void* descriptions = nullptr;
	size_t count = 0;

	rx_render_stage_descriptions(mpStage, &descriptions, &count);

	return Slice<VkAttachmentDescription>(const_cast<VkAttachmentDescription*>(
											  static_cast<const VkAttachmentDescription*>(descriptions)),
										  count);
}

void RenderStage::BuildRenderStage()
{
	if (mbIsBuilt) {
		return;
	}

	if (mbIsFinalStage) {
		AddPresentTarget();
	}

	Build(mSize / Vec2u(mSizeDivisor), false);

	mbIsBuilt = true;
}

void RenderStage::Rebuild(const Vec2u& size)
{
	mSize = GetRealTargetSize(size);

	mbIsBuilt = false;

	Build(mSize / Vec2u(mSizeDivisor), true);

	mbIsBuilt = true;
}

void RenderStage::Build(const Vec2u& final_size, bool recreate)
{
	const Vec2u size = (final_size == Target::scFullScreen) ? gGraphics->Swapchain.Extent : final_size;

	Assert(size.X > 0 && size.Y > 0);

	// The final stage draws to the swapchain images, with a framebuffer for each
	SizedArray<uint64> final_views;

	if (mbIsFinalStage) {
		SizedArray<Image>& final_images = gGraphics->Swapchain.OutputImages;

		final_views.InitCapacity(final_images.Size);

		for (const Image& image : final_images) {
			final_views.Insert(image.GetRawView());
		}
	}

	const VkResult status = static_cast<VkResult>(rx_render_stage_build(
		mpStage, gGraphics->GetDevice()->GetRustDevice(), gGraphics->GpuAllocator, size.X, size.Y, final_views.pData,
		final_views.Size, gGraphics->Swapchain.Extent.X, gGraphics->Swapchain.Extent.Y, recreate));

	if (status != VK_SUCCESS) {
		PanicVulkan("RenderStage", "Failed to build the render stage", status);
	}

	renderer::Util::SetDebugLabel(pcName, VK_OBJECT_TYPE_RENDER_PASS, GetRenderPass());
}

void RenderStage::Begin(CommandBuffer& cmd)
{
	Begin(cmd, VkRect2D {
				   .offset = { .x = static_cast<int32>(mpStage->offset_x), .y = static_cast<int32>(mpStage->offset_y) },
				   .extent = { .width = mpStage->width, .height = mpStage->height },
			   });
}

void RenderStage::Begin(CommandBuffer& cmd, const VkRect2D& render_area) { Begin(cmd, render_area, render_area); }

void RenderStage::Begin(CommandBuffer& cmd, const VkRect2D& render_area, const VkRect2D& draw_area)
{
	Assert(mbIsBuilt);

	// Pipelines leave the viewport and scissor to whoever is drawing, and the target is what decides them. They stay set
	// across pipeline binds, so this is the only place that sets them.
	rx_render_stage_begin(mpStage, gGraphics->GetDevice()->GetRustDevice(), cmd.Get(),
						  mbIsFinalStage ? gGraphics->GetImageIndex() : 0, render_area.offset.x, render_area.offset.y,
						  render_area.extent.width, render_area.extent.height, draw_area.offset.x, draw_area.offset.y,
						  draw_area.extent.width, draw_area.extent.height);

	mpEndCommandBuffer = &cmd;
}

void RenderStage::End()
{
	Assert(mpEndCommandBuffer != nullptr);

	rx_render_stage_end(mpStage, gGraphics->GetDevice()->GetRustDevice(), mpEndCommandBuffer->Cmd);
}

void RenderStage::AddTarget(eImageFormat format, VkImageUsageFlags usage, eImageAspectFlag aspect)
{
	AddTarget(Target(format, mSize / mSizeDivisor, mbIsFullscreen, usage, aspect));
}

void RenderStage::AddTarget(const Target& attachment)
{
	const RxTargetConfig config = attachment.ToRust();

	rx_render_stage_add_target(mpStage, &config, attachment.pReferenceImage);
}


void RenderStage::MarkFinalStage()
{
	AssertMsg(mbIsBuilt == false, "Cannot mark final -- Render stage was already built!");

	mbIsFinalStage = true;
}


void RenderStage::AddPresentTarget()
{
	AddTarget(Target(gGraphics->Swapchain.Surface.Format, Target::scFullScreen, true, eLoadOp::DontCare,
					 eStoreOp::Store, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));
}

} // namespace fx::renderer
