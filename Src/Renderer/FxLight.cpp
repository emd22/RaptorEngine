#include "FxLight.hpp"

#include "FxCamera.hpp"
#include "Renderer.hpp"

using VertexType = FxLight::VertexType;

void FxLight::SetLightVolume(const FxRef<FxPrimitiveMesh<VertexType>>& volume)
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
    RxFrameData* frame = Renderer->GetFrame();
    FxRef<RxDeferredRenderer>& deferred = Renderer->DeferredRenderer;

    FxMat4f MVP = mModelMatrix * camera.VPMatrix;
    MVP.FlipY();

    {
        FxLightVertPushConstants push_constants {};
        memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));

        vkCmdPushConstants(frame->LightCommandBuffer.CommandBuffer, deferred->LightingPipeline.Layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(push_constants), &push_constants);
    }
    {
        FxLightFragPushConstants push_constants {};
        memcpy(push_constants.InvView, camera.InvViewMatrix.RawData, sizeof(FxMat4f));
        memcpy(push_constants.InvProj, camera.InvProjectionMatrix.RawData, sizeof(FxMat4f));

        memcpy(push_constants.LightPos, mPosition.mData, sizeof(float32) * 4);

        push_constants.LightColor[0] = Color.X;
        push_constants.LightColor[1] = Color.Y;
        push_constants.LightColor[2] = Color.Z;
        push_constants.LightColor[3] = 1.0;

        memcpy(push_constants.PlayerPos, camera.Position.mData, sizeof(float32) * 4);
        push_constants.LightRadius = 1.0;

        vkCmdPushConstants(frame->LightCommandBuffer.CommandBuffer, deferred->LightingPipeline.Layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           sizeof(FxLightVertPushConstants), sizeof(FxLightFragPushConstants), &push_constants);
    }

    LightVolume->Render(frame->LightCommandBuffer, deferred->LightingPipeline);
}


void FxLight::RenderDebugMesh(const FxCamera& camera)
{
    if (!mDebugMesh) {
        return;
    }

    RxFrameData* frame = Renderer->GetFrame();
    FxRef<RxDeferredRenderer>& deferred = Renderer->DeferredRenderer;

    FxMat4f MVP = mModelMatrix * camera.VPMatrix;
    MVP.FlipY();

    FxDrawPushConstants push_constants {};
    memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));
    memcpy(push_constants.ModelMatrix, mModelMatrix.RawData, sizeof(FxMat4f));

    vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, deferred->GPassPipeline.Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants),
                       &push_constants);

    mDebugMesh->Render(frame->CommandBuffer, deferred->GPassPipeline);
}
