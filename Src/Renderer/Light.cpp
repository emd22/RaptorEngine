#include "Light.hpp"

#include "Camera.hpp"
#include "Engine.hpp"

#include <ObjectManager.hpp>
#include <Renderer/RxGlobals.hpp>
#include <Renderer/RxRenderBackend.hpp>

namespace fx {

using namespace fx::renderer;

using VertexType = LightBase::VertexType;

LightBase::LightBase(LightFlags flags) : Flags(flags)
{
    ObjectId = gObjectManager->GenerateObjectId();

    LogDebug("Creating light (id={})", ObjectId);
}

void LightBase::SetLightVolume(const Ref<PrimitiveMesh>& volume) { pLightVolume = volume; }

void LightBase::SetLightVolume(const Ref<MeshGen::GeneratedMesh>& volume_gen, bool create_debug_mesh)
{
    pLightVolumeGen = volume_gen;
    pLightVolume = volume_gen->AsSlimMesh();
    // Radius = LightVolume->VertexList.CalculateDimensionsFromPositions().X;

    if (create_debug_mesh) {
        mpDebugMesh = volume_gen->AsDefaultMesh();
    }
}


// void LightBase::MoveTo(const Vec3f& position)
// {
//     if ((Flags & LF_IndependentMeshPosition)) {
//         mLightPosition = position;

//         // The model matrix does not need to be updated as we won't move the mesh
//         return;
//     }

//     this->Entity::MoveTo(position);
//     mLightPosition = mPosition;
// }


// void LightBase::MoveTo(const Vec3f& position, bool force_move_mesh)
// {
//     this->MoveTo(position);

//     if (force_move_mesh && !(Flags & LF_IndependentMeshPosition)) {
//         this->Entity::MoveTo(position);
//         mLightPosition = mPosition;
//     }
// }


// void LightBase::MoveBy(const Vec3f& offset)
// {
//     if ((Flags & LF_IndependentMeshPosition)) {
//         mLightPosition += offset;

//         // The model matrix does not need to be updated as we won't move the mesh
//         return;
//     }

//     this->Entity::MoveBy(offset);
//     mLightPosition = mPosition;
// }


// void LightBase::MoveBy(const Vec3f& offset, bool force_move_mesh)
// {
//     this->MoveBy(offset);

//     if (force_move_mesh && !(Flags & LF_IndependentMeshPosition)) {
//         this->Entity::MoveBy(offset);
//         mLightPosition = mPosition;
//     }
// }

// void LightBase::Scale(const Vec3f& scale)
// {
//     this->Entity::Scale(scale);
//     Radius *= scale.X;
// }

void LightBase::Render(const PerspectiveCamera& camera, Camera* shadow_camera)
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

    gRenderer->Uniforms.Rewind();

    pPipeline->Bind(frame->CommandBuffer);

    {
        LightVertPushConstants push_constants {};
        memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(ObjectLayer::eWorldLayer).RawData, sizeof(Mat4f));


        push_constants.ObjectId = ObjectId;

        vkCmdPushConstants(frame->CommandBuffer.Get(), pPipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(push_constants), &push_constants);
    }


    gRenderer->Uniforms.WritePtr(shadow_camera->GetCameraMatrix(ObjectLayer::eWorldLayer).RawData, sizeof(Mat4f));
    gRenderer->Uniforms.WritePtr(camera.InvViewMatrix.RawData, sizeof(Mat4f));
    gRenderer->Uniforms.WritePtr(camera.InvProjectionMatrix.RawData, sizeof(Mat4f));

    // Note that the light position is packed with the light colour as the fourth component!
    gRenderer->Uniforms.WritePtr(mPosition.mData, sizeof(float32) * 3);
    gRenderer->Uniforms.Write(Color.Value);

    gRenderer->Uniforms.WritePtr(camera.Position.mData, sizeof(float32) * 3);
    gRenderer->Uniforms.Write(mRadius);

    gRenderer->Uniforms.Write(static_cast<float32>(gRenderer->Swapchain.Extent.X));
    gRenderer->Uniforms.Write(static_cast<float32>(gRenderer->Swapchain.Extent.Y));

    gRenderer->Uniforms.Write(AmbientColor.Value);

    gRenderer->Uniforms.FlushToGpu();

    pLightVolume->Render(frame->CommandBuffer, 1);
}


void LightBase::RenderDebugMesh(const PerspectiveCamera& camera)
{
    if (!mpDebugMesh) {
        return;
    }

    RxFrameData* frame = gRenderer->GetFrame();
    Ref<RxDeferredRenderer>& deferred = gRenderer->pDeferredRenderer;

    DrawPushConstants push_constants {};
    memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(ObjectLayer::eWorldLayer).RawData, sizeof(Mat4f));
    push_constants.ObjectId = ObjectId;

    vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, deferred->PlGeometry.Layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
                       &push_constants);

    mpDebugMesh->Render(frame->CommandBuffer, 1);
}


void LightBase::SetRadius(const float radius)
{
    mRadius = radius;
    SetScale(mRadius * 2);
}

LightPoint::LightPoint()
{
    pPipelineInside = &gRenderer->pDeferredRenderer->PlLightingInsideVolume;
    pPipelineOutside = &gRenderer->pDeferredRenderer->PlLightingOutsideVolume;

    Type = LightType::ePoint;
}


LightDirectional::LightDirectional()
{
    pPipeline = &gRenderer->pDeferredRenderer->PlLightingDirectional;

    Type = LightType::eDirectional;
}

void LightDirectional::Render(const PerspectiveCamera& camera, Camera* shadow_camera)
{
    if (!bEnabled) {
        return;
    }

    RxFrameData* frame = gRenderer->GetFrame();
    UpdateIfOutOfDate();

    pPipeline->Bind(frame->CommandBuffer);

    {
        LightVertPushConstants push_constants {};
        memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(ObjectLayer::eWorldLayer).RawData, sizeof(Mat4f));

        push_constants.ObjectId = ObjectId;

        vkCmdPushConstants(frame->CommandBuffer.Get(), pPipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(push_constants), &push_constants);
    }

    gRenderer->Uniforms.Rewind();

    if (shadow_camera) {
        gRenderer->Uniforms.WritePtr(shadow_camera->GetCameraMatrix(ObjectLayer::eWorldLayer).RawData, sizeof(Mat4f));
    }
    else {
        gRenderer->Uniforms.WritePtr(camera.GetCameraMatrix(ObjectLayer::eWorldLayer).RawData, sizeof(Mat4f));
    }

    gRenderer->Uniforms.WritePtr(camera.InvViewMatrix.RawData, sizeof(Mat4f));
    gRenderer->Uniforms.WritePtr(camera.InvProjectionMatrix.RawData, sizeof(Mat4f));

    // // Note that the light position is packed with the light colour as the fourth component!
    gRenderer->Uniforms.WritePtr(camera.Position.mData, sizeof(float32) * 3);
    gRenderer->Uniforms.Write(mRadius);

    gRenderer->Uniforms.WritePtr(mPosition.mData, sizeof(float32) * 3);
    gRenderer->Uniforms.Write(Color.Value);

    gRenderer->Uniforms.Write(static_cast<float32>(gRenderer->Swapchain.Extent.X));
    gRenderer->Uniforms.Write(static_cast<float32>(gRenderer->Swapchain.Extent.Y));

    gRenderer->Uniforms.Write(AmbientColor.Value);

    gRenderer->Uniforms.FlushToGpu();

    // gRenderer->Uniforms.AssertSize(sizeof(LightFragPushConstants));

    vkCmdDraw(frame->CommandBuffer.Get(), 3, 1, 0, 0);
}

} // namespace fx
