#pragma once

#include "Backend/Descriptors.hpp"
#include "Backend/Framebuffer.hpp"
#include "Backend/Image.hpp"
#include "Backend/RenderPass.hpp"
#include "Backend/Sampler.hpp"

#include <Core/SizedArray.hpp>

namespace fx::renderer {

class RenderStage
{
    static constexpr uint32 scMaxInputAttachments = 6;
    static constexpr uint32 scMaxOutputTargets = 8;
    static constexpr uint32 scMaxInputBuffers = 2;

    struct InputTarget
    {
        uint32 BindIndex = 0;
        Target* pTarget = nullptr;
        Sampler* pSampler = nullptr;
        RawGpuBuffer* pBuffer = nullptr;

        uint32 BufferOffset = 0;
        uint32 BufferRange = 0;
    };

public:
    RenderStage() = default;

    void Create(const Vec2u& size)
    {
        ClearValues.InitCapacity(scMaxOutputTargets);
        mSize = size;
    }

    void AddTarget(eImageFormat format, const Vec2u& size, VkImageUsageFlags usage, eImageAspectFlag aspect);
    void AddTarget(const Target& attachment);

    TargetList& GetTargets() { return mOutputTargets; }
    Target* GetTarget(eImageFormat format, int sub_index = 0);

    void MarkFinalStage();

    RenderPass& GetRenderPass() { return mRenderPass; }
    Framebuffer& GetFramebuffer() { return mFramebuffer; }

    /**
     * @brief Builds the Vulkan objects for the render stage. This is deferred until the user requests a renderpass or
     * attachment. This is to reduce unused render stages, allow changes before the stage is built, etc.
     */
    void BuildRenderStage();

    void Rebuild(const Vec2u& size);
    FX_FORCE_INLINE bool IsBuilt() const { return mbIsBuilt; }

    void Begin(CommandBuffer& cmd, Pipeline& pipeline);
    void End() { mRenderPass.End(); }

    ~RenderStage() = default;

private:
    void MakeClearValues();
    void CreateFinalStageFramebuffers();

    void AddPresentTarget();

public:
    SizedArray<VkClearValue> ClearValues;

private:
    TargetList mOutputTargets;
    SizedArray<InputTarget> mInputTargets;

    Framebuffer mFramebuffer;
    RenderPass mRenderPass;
    Vec2u mSize = Vec2u::sZero;

    bool mbIsBuilt : 1 = false;


    bool mbIsFinalStage = false;
    SizedArray<Framebuffer> mFinalStageFramebuffers;
};

} // namespace fx::renderer
