#include "FxLight.hpp"

#include "FxCamera.hpp"
#include "FxEngine.hpp"

using VertexType = FxLight::VertexType;

void FxLight::SetLightVolume(const FxRef<FxPrimitiveMesh<VertexType>>& volume) { LightVolume = volume; }

void FxLight::SetLightVolume(const FxRef<FxMeshGen::GeneratedMesh>& volume_gen, bool create_debug_mesh)
{
    LightVolumeGen = volume_gen;
    LightVolume = volume_gen->AsLightVolume();

    if (create_debug_mesh) {
        mDebugMesh = volume_gen->AsMesh();
    }
}


void FxLight::MoveTo(const FxVec3f& position)
{
    if ((Flags & FxLF_IndependentMeshPosition)) {
        mLightPosition = position;

        // The model matrix does not need to be updated as we won't move the mesh
        return;
    }

    this->FxEntity::MoveTo(position);
    mLightPosition = mPosition;
}


void FxLight::MoveTo(const FxVec3f& position, bool force_move_mesh)
{
    this->MoveTo(position);

    if (force_move_mesh && !(Flags & FxLF_IndependentMeshPosition)) {
        this->FxEntity::MoveTo(position);
        mLightPosition = mPosition;
    }
}


void FxLight::MoveBy(const FxVec3f& offset)
{
    if ((Flags & FxLF_IndependentMeshPosition)) {
        mLightPosition += offset;

        // The model matrix does not need to be updated as we won't move the mesh
        return;
    }

    this->FxEntity::MoveBy(offset);
    mLightPosition = mPosition;
}


void FxLight::MoveBy(const FxVec3f& offset, bool force_move_mesh)
{
    this->MoveBy(offset);

    if (force_move_mesh && !(Flags & FxLF_IndependentMeshPosition)) {
        this->FxEntity::MoveBy(offset);
        mLightPosition = mPosition;
    }
}

void FxLight::MoveLightVolumeTo(const FxVec3f& position) { this->FxEntity::MoveTo(position); }

void FxLight::Render(const FxCamera& camera)
{
    RxFrameData* frame = gRenderer->GetFrame();
    FxRef<RxDeferredRenderer>& deferred = gRenderer->DeferredRenderer;

    FxMat4f MVP = GetModelMatrix() * camera.VPMatrix;
    MVP.FlipY();

    {
        FxLightVertPushConstants push_constants {};
        memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));

        vkCmdPushConstants(frame->LightCommandBuffer.CommandBuffer, deferred->LightingPipeline.Layout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);
    }
    {
        FxLightFragPushConstants push_constants {};
        memcpy(push_constants.InvView, camera.InvViewMatrix.RawData, sizeof(FxMat4f));
        memcpy(push_constants.InvProj, camera.InvProjectionMatrix.RawData, sizeof(FxMat4f));

        // Copy the light positions to the push constants
        {
            const float* light_positions = mPosition.mData;

            // If the light type needs separate positions for the light and mesh, pass the separate light positions to
            // the shader
            if ((Flags & FxLF_IndependentMeshPosition)) {
                light_positions = mLightPosition.mData;
            }

            memcpy(push_constants.LightPos, light_positions, sizeof(float32) * 4);
        }

        push_constants.LightColor[0] = Color.X;
        push_constants.LightColor[1] = Color.Y;
        push_constants.LightColor[2] = Color.Z;
        push_constants.LightColor[3] = 1.0;

        memcpy(push_constants.PlayerPos, camera.Position.mData, sizeof(float32) * 4);
        push_constants.LightRadius = Radius;

        vkCmdPushConstants(frame->LightCommandBuffer.CommandBuffer, deferred->LightingPipeline.Layout,
                           VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(FxLightVertPushConstants),
                           sizeof(FxLightFragPushConstants), &push_constants);
    }

    LightVolume->Render(frame->LightCommandBuffer, deferred->LightingPipeline);
}


void FxLight::RenderDebugMesh(const FxCamera& camera)
{
    if (!mDebugMesh) {
        return;
    }

    RxFrameData* frame = gRenderer->GetFrame();
    FxRef<RxDeferredRenderer>& deferred = gRenderer->DeferredRenderer;

    FxMat4f MVP = GetModelMatrix() * camera.VPMatrix;
    MVP.FlipY();

    FxDrawPushConstants push_constants {};
    memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));
    memcpy(push_constants.ModelMatrix, GetModelMatrix().RawData, sizeof(FxMat4f));

    vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, deferred->GPassPipeline.Layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(push_constants), &push_constants);

    mDebugMesh->Render(frame->CommandBuffer, deferred->GPassPipeline);
}
