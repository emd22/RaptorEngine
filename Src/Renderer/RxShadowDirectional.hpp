#pragma once

#include <Math/FxVec2.hpp>
#include <Renderer/Backend/RxDescriptors.hpp>
#include <Renderer/Backend/RxFramebuffer.hpp>
#include <Renderer/Backend/RxImage.hpp>
#include <Renderer/Backend/RxPipeline.hpp>
#include <Renderer/Backend/RxRenderPass.hpp>
#include <Renderer/FxCamera.hpp>

class RxShadowDirectional
{
public:
    RxShadowDirectional() = delete;
    RxShadowDirectional(const FxVec2u& size);

    void Begin();

    void End();

public:
    FxOrthoCamera ShadowCamera;

private:
    RxFramebuffer mFramebuffer;
    RxImage mAttachment;

    RxRenderPass mRenderPass;

    RxCommandBuffer mCommandBuffer;

    RxPipeline mPipeline;

    VkDescriptorSetLayout mDsLayout;
};
