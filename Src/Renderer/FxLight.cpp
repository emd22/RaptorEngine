#include "FxLight.hpp"
#include "FxCamera.hpp"

#include "Renderer.hpp"

using VertexType = FxLight::VertexType;

void FxLight::SetLightVolume(const FxRef<FxMesh<VertexType>>& volume)
{
    LightVolume = volume;
}

void FxLight::SetLightVolume(const FxRef<FxMeshGen::GeneratedMesh>& volume_gen, bool create_debug_mesh)
{
    LightVolumeGen = volume_gen;
    LightVolume = volume_gen->AsLightVolume();

    if (create_debug_mesh) {
        mDebugMesh = volume_gen->AsMesh();
    }
}


void FxLight::Render(const FxCamera& camera) const
{
    RvkFrameData* frame = Renderer->GetFrame();
    FxRef<FxDeferredRenderer>& deferred = Renderer->DeferredRenderer;

    FxMat4f MVP = mModelMatrix * camera.VPMatrix;
    MVP.FlipY();

    {
        FxLightPushConstants push_constants{};
        memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));
        memcpy(push_constants.VPMatrix, camera.VPMatrix.RawData, sizeof(FxMat4f));
        memcpy(push_constants.LightPos, mPosition.mData, sizeof(float32) * 3);
        memcpy(push_constants.PlayerPos, camera.Position.mData, sizeof(float32) * 3);
        push_constants.LightRadius = 1.0;

        vkCmdPushConstants(frame->LightCommandBuffer.CommandBuffer, deferred->LightingPipeline.Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);
    }
    {
        FxCompositionPushConstants push_constants{};
        memcpy(push_constants.ViewInverse, camera.InvViewMatrix.RawData, sizeof(FxMat4f));
        memcpy(push_constants.ProjInverse, camera.InvProjectionMatrix.RawData, sizeof(FxMat4f));

        vkCmdPushConstants(frame->LightCommandBuffer.CommandBuffer, deferred->LightingPipeline.Layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(FxLightPushConstants), sizeof(push_constants), &push_constants);
    }

    LightVolume->Render(frame->LightCommandBuffer, deferred->LightingPipeline);
}


void FxLight::RenderDebugMesh(const FxCamera& camera)
{
    if (!mDebugMesh) {
        return;
    }

    RvkFrameData* frame = Renderer->GetFrame();
    FxRef<FxDeferredRenderer>& deferred = Renderer->DeferredRenderer;

    FxMat4f MVP = mModelMatrix * camera.VPMatrix;
    MVP.FlipY();

    FxDrawPushConstants push_constants{};
    memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));
    memcpy(push_constants.ModelMatrix, mModelMatrix.RawData, sizeof(FxMat4f));

    vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, deferred->GPassPipeline.Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);

    mDebugMesh->Render(frame->CommandBuffer, deferred->GPassPipeline);
}
