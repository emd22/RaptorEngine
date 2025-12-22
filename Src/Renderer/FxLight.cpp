#include "FxLight.hpp"

#include "FxCamera.hpp"
#include "FxEngine.hpp"

#include <FxObjectManager.hpp>
#include <Renderer/RxRenderBackend.hpp>


using VertexType = FxLightBase::VertexType;

FxLightBase::FxLightBase(FxLightFlags flags) : Flags(flags)
{
    ObjectId = gObjectManager->GenerateObjectId();

    FxLogDebug("Creating light (id={})", ObjectId);
}

void FxLightBase::SetLightVolume(const FxRef<FxPrimitiveMesh<VertexType>>& volume) { LightVolume = volume; }

void FxLightBase::SetLightVolume(const FxRef<FxMeshGen::GeneratedMesh>& volume_gen, bool create_debug_mesh)
{
    LightVolumeGen = volume_gen;
    LightVolume = volume_gen->AsPositionsMesh();
    // Radius = LightVolume->VertexList.CalculateDimensionsFromPositions().X;

    if (create_debug_mesh) {
        mDebugMesh = volume_gen->AsMesh();
    }
}


// void FxLightBase::MoveTo(const FxVec3f& position)
// {
//     if ((Flags & FxLF_IndependentMeshPosition)) {
//         mLightPosition = position;

//         // The model matrix does not need to be updated as we won't move the mesh
//         return;
//     }

//     this->FxEntity::MoveTo(position);
//     mLightPosition = mPosition;
// }


// void FxLightBase::MoveTo(const FxVec3f& position, bool force_move_mesh)
// {
//     this->MoveTo(position);

//     if (force_move_mesh && !(Flags & FxLF_IndependentMeshPosition)) {
//         this->FxEntity::MoveTo(position);
//         mLightPosition = mPosition;
//     }
// }


// void FxLightBase::MoveBy(const FxVec3f& offset)
// {
//     if ((Flags & FxLF_IndependentMeshPosition)) {
//         mLightPosition += offset;

//         // The model matrix does not need to be updated as we won't move the mesh
//         return;
//     }

//     this->FxEntity::MoveBy(offset);
//     mLightPosition = mPosition;
// }


// void FxLightBase::MoveBy(const FxVec3f& offset, bool force_move_mesh)
// {
//     this->MoveBy(offset);

//     if (force_move_mesh && !(Flags & FxLF_IndependentMeshPosition)) {
//         this->FxEntity::MoveBy(offset);
//         mLightPosition = mPosition;
//     }
// }

// void FxLightBase::Scale(const FxVec3f& scale)
// {
//     this->FxEntity::Scale(scale);
//     Radius *= scale.X;
// }

void FxLightBase::Render(const FxPerspectiveCamera& camera)
{
    if (!bEnabled) {
        return;
    }

    if (camera.Position.IntersectsSphere(mPosition, mRadius)) {
        pPipeline = pPipelineInside;
    }
    else {
        pPipeline = pPipelineOutside;
    }

    RxFrameData* frame = gRenderer->GetFrame();
    UpdateIfOutOfDate();

    pPipeline->Bind(frame->LightCommandBuffer);


    {
        FxLightVertPushConstants push_constants {};
        memcpy(push_constants.VPMatrix, camera.VPMatrix.RawData, sizeof(FxMat4f));
        push_constants.ObjectId = ObjectId;

        vkCmdPushConstants(frame->LightCommandBuffer.CommandBuffer, pPipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(push_constants), &push_constants);
    }
    {
        FxLightFragPushConstants push_constants {};
        memcpy(push_constants.InvView, camera.InvViewMatrix.RawData, sizeof(FxMat4f));
        memcpy(push_constants.InvProj, camera.InvProjectionMatrix.RawData, sizeof(FxMat4f));

        // Copy the light positions to the push constants
        memcpy(push_constants.LightPosition, mPosition.mData, sizeof(float32) * 3);


        push_constants.LightColor = Color.Value;

        memcpy(push_constants.EyePosition, camera.Position.mData, sizeof(float32) * 3);
        push_constants.LightRadius = mRadius;

        vkCmdPushConstants(frame->LightCommandBuffer.CommandBuffer, pPipeline->Layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           sizeof(FxLightVertPushConstants), sizeof(FxLightFragPushConstants), &push_constants);
    }

    LightVolume->Render(frame->LightCommandBuffer, *pPipeline);
}


void FxLightBase::RenderDebugMesh(const FxPerspectiveCamera& camera)
{
    if (!mDebugMesh) {
        return;
    }

    RxFrameData* frame = gRenderer->GetFrame();
    FxRef<RxDeferredRenderer>& deferred = gRenderer->pDeferredRenderer;

    FxDrawPushConstants push_constants {};
    memcpy(push_constants.VPMatrix, camera.VPMatrix.RawData, sizeof(FxMat4f));
    push_constants.ObjectId = ObjectId;

    vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, deferred->PlGeometry.Layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
                       &push_constants);

    mDebugMesh->Render(frame->CommandBuffer, deferred->PlGeometry);
}


void FxLightBase::SetRadius(const float radius)
{
    mRadius = radius;
    SetScale(mRadius * 2);
}

FxLightPoint::FxLightPoint()
{
    pPipelineInside = &gRenderer->pDeferredRenderer->PlLightingInsideVolume;
    pPipelineOutside = &gRenderer->pDeferredRenderer->PlLightingOutsideVolume;
}


FxLightDirectional::FxLightDirectional() { pPipeline = &gRenderer->pDeferredRenderer->PlLightingDirectional; }

void FxLightDirectional::Render(const FxPerspectiveCamera& camera)
{
    if (!bEnabled) {
        return;
    }

    RxFrameData* frame = gRenderer->GetFrame();
    UpdateIfOutOfDate();

    pPipeline->Bind(frame->LightCommandBuffer);

    {
        FxLightVertPushConstants push_constants {};
        memcpy(push_constants.VPMatrix, camera.VPMatrix.RawData, sizeof(FxMat4f));
        push_constants.ObjectId = ObjectId;

        vkCmdPushConstants(frame->LightCommandBuffer.CommandBuffer, pPipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(push_constants), &push_constants);
    }
    {
        FxLightFragPushConstants push_constants {};
        memcpy(push_constants.InvView, camera.InvViewMatrix.RawData, sizeof(FxMat4f));
        memcpy(push_constants.InvProj, camera.InvProjectionMatrix.RawData, sizeof(FxMat4f));

        // Copy the light positions to the push constants
        memcpy(push_constants.LightPosition, mPosition.mData, sizeof(float32) * 3);

        push_constants.LightColor = Color.Value;

        memcpy(push_constants.EyePosition, camera.Position.mData, sizeof(float32) * 3);
        push_constants.LightRadius = mRadius;

        vkCmdPushConstants(frame->LightCommandBuffer.CommandBuffer, pPipeline->Layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           sizeof(FxLightVertPushConstants), sizeof(FxLightFragPushConstants), &push_constants);
    }

    vkCmdDraw(frame->LightCommandBuffer.CommandBuffer, 3, 1, 0, 0);
}
