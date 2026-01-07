#pragma once

#include <Math/FxVec2.hpp>
#include <Renderer/Backend/RxDescriptors.hpp>
#include <Renderer/Backend/RxFramebuffer.hpp>
#include <Renderer/Backend/RxImage.hpp>
#include <Renderer/Backend/RxPipeline.hpp>
#include <Renderer/Backend/RxRenderPass.hpp>
#include <Renderer/FxCamera.hpp>

class RxDeferredLightingPass;

struct alignas(16) RxShadowPushConstants
{
    float32 CameraMatrix[16];
    uint32 ObjectId = 0;
};
class RxShadowDirectional
{
public:
    RxShadowDirectional() = delete;
    RxShadowDirectional(const FxVec2u& size);

    void Begin();

    void End();

    FX_FORCE_INLINE RxPipeline& GetPipeline() { return mPipeline; }
    FX_FORCE_INLINE RxCommandBuffer* GetCommandBuffer() { return mCommandBuffer; }

private:
    void UpdateDescriptorSet(int index, const RxDeferredLightingPass& lighting_pass);

public:
    FxOrthoCamera ShadowCamera;
    RxCommandBuffer* mCommandBuffer;
    FxStackArray<RxImage, RendererFramesInFlight> mAttachments;

private:
    FxStackArray<RxFramebuffer, RendererFramesInFlight> mFramebuffers;

    RxRenderPass mRenderPass;
    RxPipeline mPipeline;


    VkDescriptorSetLayout mDsLayout;
    RxDescriptorSet mDescriptorSet;

    RxDescriptorPool mDescriptorPool;
};
