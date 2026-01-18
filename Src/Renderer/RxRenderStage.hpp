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

    void Create(const FxVec2u& size);


    void AddTarget(RxImageFormat format, const FxVec2u& size, VkImageUsageFlags usage, RxImageAspectFlag aspect);

    
    RxAttachmentList& GetTargets()
    {
        return mOutputTargets;
    }

private:
    /**
     * @brief Builds the Vulkan objects for the render stage. This is deferred until the user requests a renderpass or
     * attachment. This is to reduce unused render stages, allow changes before the stage is built, etc.
     */
    void BuildRenderStage();

    void CreateOutputTargets();

private: 
    RxAttachmentList mOutputTargets;

    RxFramebuffer mFramebuffer;
    RxRenderPass mRenderPass;

    bool mbIsBuilt : 1 = false;

    FxVec2u mSize = FxVec2u::sZero;
};
