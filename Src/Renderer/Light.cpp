#include "Light.hpp"

#include "Camera.hpp"
#include "Engine.hpp"

#include <ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx {

using namespace fx::renderer;

using VertexType = LightBase::VertexType;

LightBase::LightBase(eLightFlags flags) : Flags(flags)
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

    FrameData* frame = gRenderer->GetFrame();
    UpdateIfOutOfDate();

    gRenderer->pDeferredRenderer->DsLighting.BindWithOffset(0, frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                            *pPipeline, gRenderer->LightBuffer.GetBaseOffset());

    gObjectManager->mObjectBufferDS.BindWithOffset(2, frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pPipeline,
                                                   gObjectManager->GetBaseOffset());


    pPipeline->Bind(frame->CommandBuffer);

    {
        LightVertPushConstants push_constants {};
        memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));


        push_constants.ObjectId = ObjectId;
        push_constants.LightId = gRenderer->LightBuffer.SlotIndex;

        gRenderer->SubmitPushConstants(frame->CommandBuffer, *pPipeline, eShaderType::Vertex, push_constants);
    }


    gRenderer->LightBuffer.WritePtr(shadow_camera->GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));
    gRenderer->LightBuffer.WritePtr(camera.InvViewMatrix.RawData, sizeof(Mat4f));
    gRenderer->LightBuffer.WritePtr(camera.InvProjectionMatrix.RawData, sizeof(Mat4f));

    // Note that the light position is packed with the light colour as the fourth component!
    gRenderer->LightBuffer.WritePtr(camera.Position.mData, sizeof(float32) * 3);
    gRenderer->LightBuffer.Write(mRadius);

    gRenderer->LightBuffer.WritePtr(mPosition.mData, sizeof(float32) * 3);
    gRenderer->LightBuffer.Write(Color.Value);

    gRenderer->LightBuffer.Write(static_cast<float32>(gRenderer->Swapchain.Extent.X));
    gRenderer->LightBuffer.Write(static_cast<float32>(gRenderer->Swapchain.Extent.Y));

    gRenderer->LightBuffer.Write(Color::FromRGBA(10, 10, 10, 10).Value);

    gRenderer->LightBuffer.FlushToGpu();
    gRenderer->LightBuffer.NextSlot();

    pLightVolume->Render(frame->CommandBuffer, 1);
}


void LightBase::RenderDebugMesh(const PerspectiveCamera& camera)
{
    if (!mpDebugMesh) {
        return;
    }

    FrameData* frame = gRenderer->GetFrame();

    DrawPushConstants push_constants {};
    memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));
    push_constants.ObjectId = ObjectId;

    gRenderer->SubmitPushConstants(frame->CommandBuffer, gPipelineCache->Request(ePipelineName::Geometry),
                                   eShaderType::Vertex | eShaderType::Pixel, push_constants);

    mpDebugMesh->Render(frame->CommandBuffer, 1);
}


void LightBase::SetRadius(const float radius)
{
    mRadius = radius;
    SetScale(mRadius * 2);
}

LightPoint::LightPoint()
{
    pPipelineInside = &gPipelineCache->Request(ePipelineName::LightingInsideVolume);
    pPipelineOutside = &gPipelineCache->Request(ePipelineName::LightingOutsideVolume);

    Type = eLightType::Point;
}


LightDirectional::LightDirectional()
{
    pPipeline = &gPipelineCache->Request(ePipelineName::LightingDirectional);

    Type = eLightType::Directional;
}

void LightDirectional::Render(const PerspectiveCamera& camera, Camera* shadow_camera)
{
    if (!bEnabled) {
        return;
    }

    FrameData* frame = gRenderer->GetFrame();
    UpdateIfOutOfDate();

    pPipeline->Bind(frame->CommandBuffer);

    {
        LightVertPushConstants push_constants {};
        memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));

        push_constants.ObjectId = ObjectId;
        push_constants.LightId = gRenderer->LightBuffer.SlotIndex;

        gRenderer->SubmitPushConstants(frame->CommandBuffer, *pPipeline, eShaderType::Vertex, push_constants);
    }

    if (shadow_camera) {
        gRenderer->LightBuffer.WritePtr(shadow_camera->GetCameraMatrix(eObjectLayer::WorldLayer).RawData,
                                        sizeof(Mat4f));
    }
    else {
        gRenderer->LightBuffer.WritePtr(camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));
    }

    gRenderer->LightBuffer.WritePtr(camera.InvViewMatrix.RawData, sizeof(Mat4f));
    gRenderer->LightBuffer.WritePtr(camera.InvProjectionMatrix.RawData, sizeof(Mat4f));

    // Note that the light position is packed with the light colour as the fourth component!
    gRenderer->LightBuffer.WritePtr(camera.Position.mData, sizeof(float32) * 3);
    gRenderer->LightBuffer.Write(mRadius);

    gRenderer->LightBuffer.WritePtr(mPosition.mData, sizeof(float32) * 3);
    gRenderer->LightBuffer.Write(Color.Value);

    gRenderer->LightBuffer.Write(static_cast<float32>(gRenderer->Swapchain.Extent.X));
    gRenderer->LightBuffer.Write(static_cast<float32>(gRenderer->Swapchain.Extent.Y));

    gRenderer->LightBuffer.Write(AmbientColor.Value);

    gRenderer->LightBuffer.FlushToGpu();
    gRenderer->LightBuffer.NextSlot();

    vkCmdDraw(frame->CommandBuffer.Get(), 3, 1, 0, 0);
}

} // namespace fx
