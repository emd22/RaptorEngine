#pragma once

#include "Backend/RxFramebuffer.hpp"
#include "Backend/RxImage.hpp"
#include "Backend/RxRenderPass.hpp"

#include <Core/FxSizedArray.hpp>

class RxRenderStage
{
    struct TargetOptions
    {
    };

public:
    RxRenderStage();

    void Create(RxAttachmentList& attachments, const FxVec2u& size);

    void AddTarget(RxImageType image_type, VkFormat format, VkImageUsageFlagBits usage);

private:
    /**
     * @brief Builds the Vulkan objects for the render stage. This is deferred until the user requests a renderpass or
     * attachment. This is to reduce unused render stages, allow changes before the stage is built, etc.
     */
    void BuildRenderStage();

private:
    RxAttachmentList mInputAttachments;

    RxFramebuffer mFramebuffer;
    FxSizedArray<RxAttachment> mOutputTargets;
    RxRenderPass mRenderPass;

    bool mbIsBuilt : 1 = false;

    FxVec2u mSize = FxVec2u::sZero;
};
