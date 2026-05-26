#include "RenderStage.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx::renderer {

Target* RenderStage::GetTarget(eImageFormat format, int sub_index)
{
    for (Target& attachment : mOutputTargets.Targets) {
        if (attachment.Image.Format == format) {
            if ((sub_index--) > 0) {
                continue;
            }

            return &attachment;
        }
    }

    return nullptr;
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

    mOutputTargets.CreateImages();

    mRenderPass.Create(mOutputTargets, mSize);
    renderer::Util::SetDebugLabel(pcName, VK_OBJECT_TYPE_RENDER_PASS, mRenderPass.Get());

    if (mbIsFinalStage) {
        CreateFinalStageFramebuffers();
    }
    else {
        mFramebuffer.Create(mOutputTargets.GetImageViews(), mRenderPass, mSize);
    }

    mbIsBuilt = true;
}

void RenderStage::Rebuild(const Vec2u& size)
{
    mSize = size;

    mbIsBuilt = false;

    mOutputTargets.RecreateImages();

    mFramebuffer.Destroy();
    mFinalStageFramebuffers.Free();
    mRenderPass.Destroy();

    mRenderPass.Create(mOutputTargets, mSize);
    renderer::Util::SetDebugLabel(pcName, VK_OBJECT_TYPE_RENDER_PASS, mRenderPass.Get());

    if (mbIsFinalStage) {
        CreateFinalStageFramebuffers();
    }
    else {
        mFramebuffer.Create(mOutputTargets.GetImageViews(), mRenderPass, mSize);
    }

    mbIsBuilt = true;
}

void RenderStage::Begin(CommandBuffer& cmd, Pipeline& pipeline)
{
    Assert(mbIsBuilt);

    VkFramebuffer framebuffer;

    if (mbIsFinalStage) {
        framebuffer = mFinalStageFramebuffers[gRenderer->GetImageIndex()].Get();
    }
    else {
        framebuffer = mFramebuffer.Get();
    }

    mRenderPass.Begin(&cmd, framebuffer, ClearValues);
    pipeline.Bind(cmd);
}

void RenderStage::AddTarget(eImageFormat format, const Vec2u& size, VkImageUsageFlags usage, eImageAspectFlag aspect)
{
    mOutputTargets.Add(Target(format, size, usage, aspect));
}

void RenderStage::AddTarget(const Target& attachment) { mOutputTargets.Add(attachment); }


void RenderStage::MakeClearValues()
{
    for (const Target& attachment : mOutputTargets.Targets) {
        if (attachment.bRenderPassOnly) {
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
    SizedArray<Image>& final_images = gRenderer->Swapchain.OutputImages;

    mFinalStageFramebuffers.InitSize(final_images.Size);

    SizedArray<VkImageView> image_views;
    image_views.InitSize(1);

    for (uint32 i = 0; i < final_images.Size; i++) {
        image_views[0] = final_images[i].View;
        mFinalStageFramebuffers[i].Create(image_views, mRenderPass, gRenderer->Swapchain.Extent);
    }
}


void RenderStage::MarkFinalStage()
{
    AssertMsg(mbIsBuilt == false, "Cannot mark final -- Render stage was already built!");

    mbIsFinalStage = true;
}


void RenderStage::AddPresentTarget()
{
    mOutputTargets.Add(Target(gRenderer->Swapchain.Surface.Format, Target::scFullScreen, eLoadOp::DontCare,
                              eStoreOp::Store, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR));
}

} // namespace fx::renderer
