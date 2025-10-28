#pragma once

#include "Backend/RxRenderPass.hpp"
#include "RxAttachment.hpp"

#include <Core/FxItemCache.hpp>

using RxRenderPassId = uint64;


struct RxRenderPassCacheSection : public FxItemCacheSection_MultiItem<RxRenderPass>
{
};

using RxRenderPassCacheType = FxItemCache<RxRenderPassId, RxRenderPass, RxRenderPassCacheSection>;

class RxRenderPassCache : public RxRenderPassCacheType
{
public:
    static constexpr int scMaxRenderPassesPerId = 4;

    using Handle = FxItemCacheHandle<RxRenderPassId, RxRenderPass, RxRenderPassCacheType>;

public:
    RxRenderPassCache() { Create(scMaxRenderPassesPerId); };

    static inline RxRenderPassId GetRenderPassId(const RxAttachmentList& attachments)
    {
        auto& ab = attachments.Attachments;

        union
        {
            struct alignas(8)
            {
                uint32 Hash;
                uint8 Count;
            };

            uint64 Value;
        } id = {
            .Count = static_cast<uint8>(ab.Size),
            .Hash = FxHashStr(reinterpret_cast<char*>(ab.Data), ab.GetSizeInBytes()),
        };

        return id.Value;
    }


    RxRenderPassId CreateRenderPass(RxAttachmentList& attachment_list);
    Handle Request(RxRenderPassId id);
    void Release(RxRenderPassId id, RxRenderPass* render_pass);

    ~RxRenderPassCache() override {}
};
