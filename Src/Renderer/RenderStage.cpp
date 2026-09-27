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
	for (Target& attachment : mOutputTargets.Targets) {
		if (attachment.Image.Info.Format == format) {
			if ((sub_index--) > 0) {
				continue;
			}

			return &attachment;
		}
	}

	return nullptr;
}

int32 RenderStage::GetTargetIndex(eImageFormat format, int32 sub_index)
{
	for (int32 i = 0; i < mOutputTargets.Targets.Size; i++) {
		const Target& target = mOutputTargets.Targets[i];

		if (target.Image.Info.Format == format) {
			if ((sub_index--) > 0) {
				continue;
			}

			return i;
		}
	}

	return -1;
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
	const VkViewport viewport = {
		.x = static_cast<float32>(draw_area.offset.x),
		.y = static_cast<float32>(draw_area.offset.y),
		.width = static_cast<float32>(draw_area.extent.width),
		.height = static_cast<float32>(draw_area.extent.height),
		// Flipped depth range, the engine uses reverse Z
		.minDepth = 1.0f,
		.maxDepth = 0.0f,
	};

	vkCmdSetViewport(cmd.Get(), 0, 1, &viewport);
	vkCmdSetScissor(cmd.Get(), 0, 1, &draw_area);
}

void RenderStage::AddTarget(eImageFormat format, VkImageUsageFlags usage, eImageAspectFlag aspect)
{
	mOutputTargets.Add(Target(format, mSize / mSizeDivisor, mbIsFullscreen, usage, aspect));
}

void RenderStage::AddTarget(const Target& attachment) { mOutputTargets.Add(attachment); }


void RenderStage::MakeClearValues()
{
	for (const Target& attachment : mOutputTargets.Targets) {
		if (attachment.bRenderPassOnly || attachment.LoadOp != eLoadOp::Clear) {
			continue;
		}

		if (attachment.Aspect == eImageAspectFlag::Depth) {
			ClearValues.Insert(VkClearValue { .depthStencil = { 0.0f, 0U } });
		}
		else if (attachment.Aspect == eImageAspectFlag::Color) {
			ClearValues.Insert(VkClearValue { .color = { { 0.0f, 0.0f, 0.0f, 0.0f } } });
		}
	}
}


void RenderStage::CreateFinalStageFramebuffers()
{
	SizedArray<Image>& final_images = gGraphics->Swapchain.OutputImages;

	mFinalStageFramebuffers.InitSize(final_images.Size);

	SizedArray<VkImageView> image_views;
	image_views.InitSize(1);

	for (uint32 i = 0; i < final_images.Size; i++) {
		image_views[0] = final_images[i].View;
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
