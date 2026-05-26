#pragma once

#include <Math/Vec2.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/Framebuffer.hpp>
#include <Renderer/Backend/Image.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/Backend/RenderPass.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/RenderStage.hpp>

namespace fx::renderer {

class DeferredLightingPass;

struct alignas(16) ShadowPushConstants
{
    float32 CameraMatrix[16];
    uint32 ObjectId = 0;
};
class ShadowDirectional
{
public:
    ShadowDirectional() = delete;
    ShadowDirectional(const Vec2u& size);

    void Begin();

    void End();

    // FX_FORCE_INLINE Pipeline& GetPipeline() { return mPipeline; }
    // FX_FORCE_INLINE Pipeline& GetSkinnedPipeline() { return mPipelineSkinned; }

    ~ShadowDirectional() = default;

private:
    void UpdateLightDescriptors();

public:
    OrthoCamera ShadowCamera;

    RenderStage RenderStage;

private:
    // Pipeline mPipeline;
    // Pipeline mPipelineSkinned;
};

} // namespace fx::renderer
