#include "RxRenderStage.hpp"


RxRenderStage::RxRenderStage() {}

void RxRenderStage::Create(RxAttachmentList& attachments, const FxVec2u& size)
{
    mSize = size;

    if (mOutputTargets.IsEmpty()) {
        FxLogError("No output targets specified for RxRenderStage!");
        return;
    }
}


void RxRenderStage::BuildRenderStage()
{
    if (mbIsBuilt) {
        return;
    }


    // mRenderPass.Create(attachments, size);

    mbIsBuilt = true;
}
