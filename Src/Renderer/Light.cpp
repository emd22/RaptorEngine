/*
 * File:        Light.cpp
 * Author:      emd22
 * Created:     08/07/2025
 * Description: Definitions for lights in the renderer.
 */

#include "Light.hpp"

#include "Camera.hpp"
#include "Engine.hpp"

#include <Object/ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/PipelineCache.hpp>

namespace fx {

using namespace fx::renderer;

using VertexType = LightBase::VertexType;

/////////////////////////////////////
// Base Light
/////////////////////////////////////

LightBase::LightBase(eLightFlags flags) : Flags(flags)
{
	this->ID = gObjectManager->NewObjectID("Light");
	LogDebug("Creating light (id={})", this->ID);
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

	FrameData* frame = gGraphics->GetFrame();
	UpdateIfOutOfDate();


	gGraphics->LightBuffer.WritePtr(shadow_camera->GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));
	gGraphics->LightBuffer.WritePtr(camera.InvViewMatrix.RawData, sizeof(Mat4f));
	gGraphics->LightBuffer.WritePtr(camera.InvProjectionMatrix.RawData, sizeof(Mat4f));

	// Note that the light position is packed with the light colour as the fourth component!
	gGraphics->LightBuffer.WritePtr(camera.Position.mData, sizeof(float32) * 3);
	gGraphics->LightBuffer.Write(mRadius);

	gGraphics->LightBuffer.WritePtr(mPosition.mData, sizeof(float32) * 3);
	gGraphics->LightBuffer.Write(Color.Value);

	gGraphics->LightBuffer.Write(static_cast<float32>(gGraphics->Swapchain.Extent.X));
	gGraphics->LightBuffer.Write(static_cast<float32>(gGraphics->Swapchain.Extent.Y));

	gGraphics->LightBuffer.Write(Color::FromRGBA(10, 10, 10, 10).Value);
	gGraphics->LightBuffer.Write(static_cast<uint32>(Type));

	gGraphics->LightBuffer.FlushToGpu();
	gGraphics->LightBuffer.NextSlot();

	// pLightVolume->Render(frame->CmdBuffer, 1);
}


void LightBase::RenderDebugMesh(const PerspectiveCamera& camera)
{
	if (!mpDebugMesh) {
		return;
	}

	FrameData* frame = gGraphics->GetFrame();

	DrawPushConstants push_constants { .TargetSize = { gGraphics->Swapchain.Extent.X, gGraphics->Swapchain.Extent.Y } };
	memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));
	push_constants.ObjectId = ID.GetID();
	push_constants.TileColumns = gGraphics->pRenderer->GetLightTileColumns();

	gGraphics->SubmitPushConstants(frame->CmdBuffer, gPipelineCache->Request(ePipelineName::Geometry),
								   eShaderType::Vertex | eShaderType::Pixel, push_constants);

	mpDebugMesh->Render(frame->CmdBuffer, 1);
}


void LightBase::SetRadius(const float radius)
{
	mRadius = radius;
	SetScale(mRadius * 2);
}

LightPoint::LightPoint() { Type = eLightType::Point; }


/////////////////////////////////////
// Directional Light
/////////////////////////////////////

LightDirectional::LightDirectional() { Type = eLightType::Directional; }

void LightDirectional::Render(const PerspectiveCamera& camera, Camera* shadow_camera)
{
	if (!bEnabled) {
		return;
	}

	FrameData* frame = gGraphics->GetFrame();
	UpdateIfOutOfDate();

	// gPipelineCache->AddBufferOffset(0, gRenderer->LightBuffer.GetBaseOffset());
	// gPipelineCache->AddBufferOffset(1, gObjectManager->GetBaseOffset());
	// gPipelineCache->Bind(ePipelineName::LightingDirectional, frame->CmdBuffer);
	// pPipeline->Bind(frame->CmdBuffer);

	// {
	// 	LightVertPushConstants push_constants {};
	// 	memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));

	// 	push_constants.ObjectId = ID.GetID();
	// 	push_constants.LightId = gRenderer->LightBuffer.SlotIndex;

	// 	gRenderer->SubmitPushConstants(frame->CmdBuffer, gPipelineCache->Request(ePipelineName::LightingDirectional),
	// 								   eShaderType::Vertex, push_constants);
	// }

	if (shadow_camera) {
		gGraphics->LightBuffer.WritePtr(shadow_camera->GetCameraMatrix(eObjectLayer::WorldLayer).RawData,
										sizeof(Mat4f));
	}
	else {
		gGraphics->LightBuffer.WritePtr(camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));
	}

	gGraphics->LightBuffer.WritePtr(camera.InvViewMatrix.RawData, sizeof(Mat4f));
	gGraphics->LightBuffer.WritePtr(camera.InvProjectionMatrix.RawData, sizeof(Mat4f));

	// Note that the light position is packed with the light colour as the fourth component!
	gGraphics->LightBuffer.WritePtr(camera.Position.mData, sizeof(float32) * 3);
	gGraphics->LightBuffer.Write(mRadius);

	gGraphics->LightBuffer.WritePtr(mPosition.mData, sizeof(float32) * 3);
	gGraphics->LightBuffer.Write(Color.Value);

	gGraphics->LightBuffer.Write(static_cast<float32>(gGraphics->Swapchain.Extent.X));
	gGraphics->LightBuffer.Write(static_cast<float32>(gGraphics->Swapchain.Extent.Y));

	gGraphics->LightBuffer.Write(AmbientColor.Value);
	gGraphics->LightBuffer.Write(static_cast<uint32>(Type));

	gGraphics->LightBuffer.FlushToGpu();
	gGraphics->LightBuffer.NextSlot();

	// vkCmdDraw(frame->CmdBuffer.Get(), 3, 1, 0, 0);
}

} // namespace fx
