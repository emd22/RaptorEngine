#pragma once

#include "Backend/RvkImage.hpp"
#include "Backend/RvkSampler.hpp"
#include "Backend/RvkFramebuffer.hpp"
#include "Backend/RvkDescriptors.hpp"

#include "Backend/RvkFrameData.hpp"


class FxDeferredRenderer;

class FxDeferredGPass
{
public:
    void Create(FxDeferredRenderer* renderer, const Vec2u& extent);
    void Destroy();

    void Begin();
    void End();
    void Submit();

    void SubmitUniforms(const RvkUniformBufferObject& ubo);
    void BuildDescriptorSets();

public:
    // G-Pass attachments
    RvkImage ColorAttachment;
    RvkImage PositionsAttachment;
    RvkImage DepthAttachment;

    // G-Pass samplers
    RvkSampler ColorSampler;
    RvkSampler PositionsSampler;

    RvkFramebuffer Framebuffer;

    RvkDescriptorPool DescriptorPool;
    RvkDescriptorSet DescriptorSet;

    RvkRawGpuBuffer<RvkUniformBufferObject> UniformBuffer;

private:
    RvkGraphicsPipeline* mGPassPipeline = nullptr;
    FxDeferredRenderer* mRendererInst = nullptr;
};


class FxDeferredRenderer
{
public:
    void Create(const Vec2u& extent);

    void Destroy();

    ~FxDeferredRenderer()
    {
        Destroy();
    }

    FxDeferredGPass* GetCurrentGPass();

private:
    void CreateGPassPipeline();

public:
    RvkGraphicsPipeline GPassPipeline;
    FxStackArray<FxDeferredGPass, RendererFramesInFlight> GPasses;
};
