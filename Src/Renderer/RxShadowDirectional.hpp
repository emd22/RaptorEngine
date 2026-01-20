#pragma once

#include <Math/FxVec2.hpp>
#include <Renderer/Backend/RxDescriptors.hpp>
#include <Renderer/Backend/RxFramebuffer.hpp>
#include <Renderer/Backend/RxImage.hpp>
#include <Renderer/Backend/RxPipeline.hpp>
#include <Renderer/Backend/RxRenderPass.hpp>
#include <Renderer/FxCamera.hpp>
#include <Renderer/RxRenderStage.hpp>

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

private:
    void UpdateDescriptorSet(int index, const RxDeferredLightingPass& lighting_pass);

public:
    FxOrthoCamera ShadowCamera;

    RxRenderStage<1> RenderStage;

private:
    RxPipeline mPipeline;

    VkDescriptorSetLayout mDsLayout;
    RxDescriptorSet mDescriptorSet;

    RxDescriptorPool mDescriptorPool;
};
