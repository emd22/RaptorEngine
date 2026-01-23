#pragma once

#include "Backend/RxDescriptors.hpp"
#include "Backend/RxFramebuffer.hpp"
#include "Backend/RxImage.hpp"
#include "Backend/RxRenderPass.hpp"
#include "Backend/RxSampler.hpp"

#include <Core/FxSizedArray.hpp>

class RxRenderStage
{
    static constexpr uint32 scMaxInputAttachments = 6;
    static constexpr uint32 scMaxInputBuffers = 2;

    struct InputTarget
    {
        uint32 BindIndex = 0;
        RxAttachment* pTarget = nullptr;
        RxSampler* pSampler = nullptr;
        RxRawGpuBuffer* pBuffer = nullptr;

        uint32 BufferOffset = 0;
        uint32 BufferRange = 0;
    };

public:
    RxRenderStage() = default;

    void Create(const FxVec2u& size) { mSize = size; }

    void AddTarget(RxImageFormat format, const FxVec2u& size, VkImageUsageFlags usage, RxImageAspectFlag aspect);

    RxAttachmentList& GetTargets() { return mOutputTargets; }
    RxAttachment* GetTarget(RxImageFormat format, int sub_index = 0);

    void AddInputBuffer(uint32 bind_index, RxRawGpuBuffer* buffer, uint32 offset, uint32 range);
    void AddInputTarget(uint32 bind_index, RxAttachment* target, RxSampler* sampler);

    void BuildInputDescriptors(RxDescriptorSet* out);

    void MarkFinalStage();

    RxRenderPass& GetRenderPass() { return mRenderPass; }

    /**
     * @brief Builds the Vulkan objects for the render stage. This is deferred until the user requests a renderpass or
     * attachment. This is to reduce unused render stages, allow changes before the stage is built, etc.
     */
    void BuildRenderStage();

    void Begin(RxCommandBuffer& cmd, RxPipeline& pipeline);

    void End() { mRenderPass.End(); }

private:
    void MakeClearValues();
    void CreateFinalStageFramebuffers();

public:
    FxSizedArray<VkClearValue> ClearValues;

private:
    RxAttachmentList mOutputTargets;
    FxSizedArray<InputTarget> mInputTargets;

    RxFramebuffer mFramebuffer;
    RxRenderPass mRenderPass;
    FxVec2u mSize = FxVec2u::sZero;

    bool mbIsBuilt : 1 = false;


    bool mbIsFinalStage = false;
    FxSizedArray<RxFramebuffer> mFinalStageFramebuffers;
};
