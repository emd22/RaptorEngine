#include "RxRenderStage.hpp"


RxRenderStage::RxRenderStage() {}

void RxRenderStage::Create(const FxVec2u& size)
{
    mSize = size;
}

void RxRenderStage::AddTarget(RxImageFormat format, const FxVec2u& size, VkImageUsageFlags usage, RxImageAspectFlag aspect)
{
    mOutputTargets.Add(RxAttachment(format, size, usage, aspect));
}



void RxRenderStage::CreateOutputTargets()
{ mOutputTargets.CreateImages(); }


void RxRenderStage::BuildRenderStage()
{
    if (mbIsBuilt) {
        return;
    }

    mRenderPass.Create(mOutputTargets, mSize);
    mFramebuffer.Create(mOutputTargets.GetImageViews(), mRenderPass, mSize);

    mbIsBuilt = true;
}
