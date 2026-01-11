#include "RxAttachment.hpp"


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
    return *this;
}
