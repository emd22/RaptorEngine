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

    gRenderer->ShaderUniform.Rewind();

    pPipeline->Bind(frame->CommandBuffer);

    {
        LightVertPushConstants push_constants {};
        memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));


        push_constants.ObjectId = ObjectId;

        vkCmdPushConstants(frame->CommandBuffer.Get(), pPipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(push_constants), &push_constants);
    }


    gRenderer->ShaderUniform.WritePtr(shadow_camera->GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));
    gRenderer->ShaderUniform.WritePtr(camera.InvViewMatrix.RawData, sizeof(Mat4f));
    gRenderer->ShaderUniform.WritePtr(camera.InvProjectionMatrix.RawData, sizeof(Mat4f));

    // Note that the light position is packed with the light colour as the fourth component!
    gRenderer->ShaderUniform.WritePtr(mPosition.mData, sizeof(float32) * 3);
    gRenderer->ShaderUniform.Write(Color.Value);

    gRenderer->ShaderUniform.WritePtr(camera.Position.mData, sizeof(float32) * 3);
    gRenderer->ShaderUniform.Write(mRadius);

    gRenderer->ShaderUniform.Write(static_cast<float32>(gRenderer->Swapchain.Extent.X));
    gRenderer->ShaderUniform.Write(static_cast<float32>(gRenderer->Swapchain.Extent.Y));

    gRenderer->ShaderUniform.Write(AmbientColor.Value);

    gRenderer->ShaderUniform.FlushToGpu();

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

    Pipeline* pl = gPipelineCache->Request(ePipelineName::Geometry);

    gRenderer->SubmitPushConstants(frame->CommandBuffer, *pl, eShaderType::Vertex | eShaderType::Pixel, push_constants);

    // vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, pl->Layout,
    //                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
    //                    &push_constants);

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

    Type = eLightType::Point;
}


LightDirectional::LightDirectional()
{
    pPipeline = &gRenderer->pDeferredRenderer->PlLightingDirectional;

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

        vkCmdPushConstants(frame->CommandBuffer.Get(), pPipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(push_constants), &push_constants);
    }

    gRenderer->ShaderUniform.Rewind();

    if (shadow_camera) {
        gRenderer->ShaderUniform.WritePtr(shadow_camera->GetCameraMatrix(eObjectLayer::WorldLayer).RawData,
                                          sizeof(Mat4f));
    }
    else {
        gRenderer->ShaderUniform.WritePtr(camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));
    }

    gRenderer->ShaderUniform.WritePtr(camera.InvViewMatrix.RawData, sizeof(Mat4f));
    gRenderer->ShaderUniform.WritePtr(camera.InvProjectionMatrix.RawData, sizeof(Mat4f));

    // // Note that the light position is packed with the light colour as the fourth component!
    gRenderer->ShaderUniform.WritePtr(camera.Position.mData, sizeof(float32) * 3);
    gRenderer->ShaderUniform.Write(mRadius);

    gRenderer->ShaderUniform.WritePtr(mPosition.mData, sizeof(float32) * 3);
    gRenderer->ShaderUniform.Write(Color.Value);

    gRenderer->ShaderUniform.Write(static_cast<float32>(gRenderer->Swapchain.Extent.X));
    gRenderer->ShaderUniform.Write(static_cast<float32>(gRenderer->Swapchain.Extent.Y));

    gRenderer->ShaderUniform.Write(AmbientColor.Value);

    gRenderer->ShaderUniform.FlushToGpu();

    // gRenderer->Uniforms.AssertSize(sizeof(LightFragPushConstants));

    vkCmdDraw(frame->CommandBuffer.Get(), 3, 1, 0, 0);
}

} // namespace fx
