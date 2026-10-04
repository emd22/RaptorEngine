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

void RenderStage::Create(const char* name, const Vec2u& size, eSizeDivisor size_divisor)
{
	pcName = name;

	ClearValues.InitCapacity(scMaxOutputTargets);

	if (size == Target::scFullScreen) {
		mbIsFullscreen = true;
	}

	mSize = GetRealTargetSize(size);
	mSizeDivisor = static_cast<uint32>(size_divisor);
}

Target* RenderStage::GetTarget(eImageFormat format, int32 sub_index)
{
	const int32 index = GetTargetIndex(format, sub_index);

	return (index >= 0) ? &mOutputTargets.Targets[index] : nullptr;
}

int32 RenderStage::GetTargetIndex(eImageFormat format, int32 sub_index)
{
	std::vector<uint16> formats;

	for (const Target& target : mOutputTargets.Targets) {
		formats.push_back(static_cast<uint16>(target.Image.GetFormat()));
	}

	return rx_find_format_index(formats.data(), formats.size(), static_cast<uint16>(format), sub_index);
}


void RenderStage::BuildRenderStage()
{
	if (mbIsBuilt) {
		return;
	}

	if (mbIsFinalStage) {
		AddPresentTarget();
	}

	// If there are no clear values already defined, generate them
	if (ClearValues.Size == 0) {
		MakeClearValues();
	}

	const Vec2u final_size = mSize / Vec2u(mSizeDivisor);

	mOutputTargets.CreateImages(final_size);

	mRenderPass.Create(mOutputTargets, final_size);
	renderer::Util::SetDebugLabel(pcName, VK_OBJECT_TYPE_RENDER_PASS, mRenderPass.Get());

	if (mbIsFinalStage) {
		CreateFinalStageFramebuffers();
	}
	else {
		mFramebuffer.Create(mOutputTargets.GetImageViews(), mRenderPass, final_size);
	}

	mbIsBuilt = true;
}

void RenderStage::Rebuild(const Vec2u& size)
{
	mSize = GetRealTargetSize(size);

	mbIsBuilt = false;

	const Vec2u final_size = mSize / Vec2u(mSizeDivisor);

	mOutputTargets.RecreateImages(final_size);

	mFramebuffer.Destroy();
	mFinalStageFramebuffers.Free();
	mRenderPass.Destroy();

	mRenderPass.Create(mOutputTargets, final_size);
	renderer::Util::SetDebugLabel(pcName, VK_OBJECT_TYPE_RENDER_PASS, mRenderPass.Get());

	if (mbIsFinalStage) {
		CreateFinalStageFramebuffers();
	}
	else {
		mFramebuffer.Create(mOutputTargets.GetImageViews(), mRenderPass, final_size);
	}

	mbIsBuilt = true;
}

void RenderStage::Begin(CommandBuffer& cmd)
{
	const Vec2u size = mRenderPass.Size;
	const Vec2u offset = mRenderPass.Offset;

	Begin(cmd, VkRect2D {
				   .offset = { .x = static_cast<int32>(offset.X), .y = static_cast<int32>(offset.Y) },
				   .extent = { .width = size.X, .height = size.Y },
			   });
}

void RenderStage::Begin(CommandBuffer& cmd, const VkRect2D& render_area) { Begin(cmd, render_area, render_area); }

void RenderStage::Begin(CommandBuffer& cmd, const VkRect2D& render_area, const VkRect2D& draw_area)
{
	Assert(mbIsBuilt);

	VkFramebuffer framebuffer;

	if (mbIsFinalStage) {
		framebuffer = mFinalStageFramebuffers[gGraphics->GetImageIndex()].Get();
	}
	else {
		framebuffer = mFramebuffer.Get();
	}

	mRenderPass.Begin(&cmd, framebuffer, ClearValues, render_area);

	// Pipelines leave the viewport and scissor to whoever is drawing, and the target is what decides them. They stay set
	// across pipeline binds, so this is the only place that sets them.
	rx_gpu_cmd_set_viewport_scissor(gGraphics->GetDevice()->GetRustDevice(), cmd.Get(), draw_area.offset.x,
									draw_area.offset.y, draw_area.extent.width, draw_area.extent.height);
}

void RenderStage::AddTarget(eImageFormat format, VkImageUsageFlags usage, eImageAspectFlag aspect)
{
	mOutputTargets.Add(Target(format, mSize / mSizeDivisor, mbIsFullscreen, usage, aspect));
}

void RenderStage::AddTarget(const Target& attachment) { mOutputTargets.Add(attachment); }


void RenderStage::MakeClearValues()
{
	std::vector<RxClearTarget> targets;

	for (const Target& attachment : mOutputTargets.Targets) {
		targets.push_back(RxClearTarget { .aspect = static_cast<uint32>(attachment.Aspect),
										  .load_op = static_cast<int32>(attachment.LoadOp),
										  .render_pass_only = attachment.bRenderPassOnly });
	}

	std::vector<VkClearValue> values(targets.size());

	const size_t count = rx_clear_values(targets.data(), targets.size(), values.data(), values.size());

	for (size_t i = 0; i < count; i++) {
		ClearValues.Insert(values[i]);
	}
}


void RenderStage::CreateFinalStageFramebuffers()
{
	SizedArray<Image>& final_images = gGraphics->Swapchain.OutputImages;

	mFinalStageFramebuffers.InitSize(final_images.Size);

	SizedArray<VkImageView> image_views;
	image_views.InitSize(1);

	for (uint32 i = 0; i < final_images.Size; i++) {
		image_views[0] = final_images[i].GetView();
		mFinalStageFramebuffers[i].Create(image_views, mRenderPass, gGraphics->Swapchain.Extent);
	}
}


void RenderStage::MarkFinalStage()
{
	AssertMsg(mbIsBuilt == false, "Cannot mark final -- Render stage was already built!");

	mbIsFinalStage = true;
}


void RenderStage::AddPresentTarget()
{
	mOutputTargets.Add(Target(gGraphics->Swapchain.Surface.Format, Target::scFullScreen, true, eLoadOp::DontCare,
							  eStoreOp::Store, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));
}

} // namespace fx::renderer
