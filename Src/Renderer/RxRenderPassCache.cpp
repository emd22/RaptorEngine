#include "RxRenderPassCache.hpp"

void RxRenderPassCache::CreateRenderPass(RxAttachmentList& attachment_list)
{
    RxRenderPassId id = GetRenderPassId(attachment_list);

    RxRenderPassCacheSection* cs = RequestCacheSection(id);

    // Cache section could not be created
    if (!cs) {
        FxLogError("Error creating render pass cache section!");
        return;
    }

    RxRenderPass* rp = cs->Items.Insert();
    rp->Create2(attachment_list);
}

RxRenderPassCache::Handle RxRenderPassCache::Request(RxRenderPassId id)
{
    RxRenderPass* rp = RequestGenericItem(id);
    FxDebugAssert(rp != nullptr);

    return RxRenderPassCache::Handle(this, rp, id);
}

void RxRenderPassCache::Release(RxRenderPassId id, RxRenderPass* render_pass) { ReleaseGenericItem(id, render_pass); }
