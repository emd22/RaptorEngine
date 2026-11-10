#include "DebugDraw.hpp"

#include <Asset/MeshGen.hpp>
#include <Core/Log.hpp>
#include <Core/RefUtil.hpp>
#include <Object/ObjectLayer.hpp>
#include <Renderer/Backend/Commands.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/Camera/BaseCamera.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/PipelineNames.hpp>
#include <cstring>

namespace fx::renderer {

namespace {

ePipelineName GetShapePipeline(eDebugShape shape)
{
	return (shape == eDebugShape::SolidBox) ? ePipelineName::DebugSolid : ePipelineName::DebugLayer;
}

} // namespace

void DebugDraw::Draw(eDebugShape shape, const FMat4& world_matrix, Color color)
{
	if (mShapes.Size >= scMaxShapes) {
		if (!bWarnedAboutOverflow) {
			LogWarning("Debug draw queue is full ({} shapes), dropping the rest until it is drawn", scMaxShapes);
			bWarnedAboutOverflow = true;
		}
		return;
	}

	mShapes.Insert(QueuedShape { .WorldMatrix = world_matrix, .Color = color.AsUInt(), .Shape = shape });
}

void DebugDraw::Line(const FVec3 from, const FVec3 to, Color color)
{
	const FVec3 delta = to - from;

	const FMat4 world_matrix(FVec4(delta.X, delta.Y, delta.Z, 0.0f), FVec4(0.0f, 0.0f, 0.0f, 0.0f),
							 FVec4(0.0f, 0.0f, 0.0f, 0.0f), FVec4(from.X, from.Y, from.Z, 1.0f));

	Draw(eDebugShape::Line, world_matrix, color);
}

static FMat4 MakeBoxMatrix(const FVec3 center, const FVec3 half_extent, const FQuat rotation)
{
	return FMat4::AsScale(half_extent) * FMat4::AsRotation(rotation) * FMat4::AsTranslation(center);
}

void DebugDraw::WireBox(const FVec3 center, const FVec3 half_extent, const FQuat rotation, Color color)
{
	WireBox(MakeBoxMatrix(center, half_extent, rotation), color);
}

void DebugDraw::WireBox(const BBox& box, Color color)
{
	WireBox((box.Min + box.Max) * 0.5f, (box.Max - box.Min) * 0.5f, FQuat::scIdentity, color);
}

void DebugDraw::SolidBox(const FVec3 center, const FVec3 half_extent, const FQuat rotation, Color color)
{
	SolidBox(MakeBoxMatrix(center, half_extent, rotation), color);
}

void DebugDraw::SolidBox(const BBox& box, Color color)
{
	SolidBox((box.Min + box.Max) * 0.5f, (box.Max - box.Min) * 0.5f, FQuat::scIdentity, color);
}

void DebugDraw::Overlay(const Ref<PrimitiveMesh>& mesh, const FMat4& world_matrix, Color color)
{
	if (!mesh.IsValid() || mesh->VertexList.VertexType == eVertexType::Slim || mesh->VertexList.IsSkinned()) {
		return;
	}

	if (mOverlays.Size >= scMaxShapes) {
		if (!bWarnedAboutOverflow) {
			LogWarning("Debug overlay queue is full ({} meshes), dropping the rest until it is drawn", scMaxShapes);
			bWarnedAboutOverflow = true;
		}
		return;
	}

	mOverlays.Insert(QueuedOverlay { .Mesh = mesh, .WorldMatrix = world_matrix, .Color = color.AsUInt() });
}

PrimitiveMesh& DebugDraw::GetMesh(eDebugShape shape)
{
	Ref<PrimitiveMesh>& mesh = mMeshes[static_cast<uint32>(shape)];

	if (!mesh.IsValid()) {
		switch (shape) {
		case eDebugShape::Line:
			mesh = MeshGen::MakeLine()->AsMesh(eVertexType::Slim);
			break;
		case eDebugShape::WireBox:
			mesh = MeshGen::MakeWireframeBox()->AsMesh(eVertexType::Slim);
			break;
		case eDebugShape::SolidBox:
		default:
			mesh = MeshGen::MakeCube({})->AsMesh(eVertexType::Slim);
			break;
		}
	}

	return *mesh;
}

void DebugDraw::RenderOverlays(const CommandBuffer& cmd, const FMat4& camera_matrix)
{
	Pipeline& pipeline = gPipelineCache->Request(ePipelineName::DebugOverlay);
	pipeline.Bind(cmd);

	DebugLayerPushConstants push_constants {};

	for (const QueuedOverlay& overlay : mOverlays) {
		const FMat4 combined_matrix = overlay.WorldMatrix * camera_matrix;
		memcpy(push_constants.CombinedMatrix, combined_matrix.RawData, sizeof(push_constants.CombinedMatrix));
		push_constants.DebugColor = overlay.Color;

		gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, push_constants);
		overlay.Mesh->Render(cmd, 1);
	}
}

void DebugDraw::Render(const CommandBuffer& cmd, const BaseCamera& camera)
{
	const FMat4& camera_matrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);

	if (mOverlays.Size > 0) {
		RenderOverlays(cmd, camera_matrix);
	}

	if (mShapes.Size > 0) {
		DebugLayerPushConstants push_constants {};

		// A pipeline and mesh are bound once per shape kind instead of once per shape
		for (uint32 kind = 0; kind < static_cast<uint32>(eDebugShape::Count); kind++) {
			const eDebugShape shape = static_cast<eDebugShape>(kind);

			Pipeline* pipeline = nullptr;
			PrimitiveMesh* mesh = nullptr;

			for (const QueuedShape& queued : mShapes) {
				if (queued.Shape != shape) {
					continue;
				}

				if (pipeline == nullptr) {
					pipeline = &gPipelineCache->Request(GetShapePipeline(shape));
					pipeline->Bind(cmd);

					mesh = &GetMesh(shape);
				}

				const FMat4 combined_matrix = queued.WorldMatrix * camera_matrix;
				memcpy(push_constants.CombinedMatrix, combined_matrix.RawData, sizeof(push_constants.CombinedMatrix));
				push_constants.DebugColor = queued.Color;

				gGraphics->SubmitPushConstants(cmd, *pipeline, eShaderType::Vertex, push_constants);
				mesh->Render(cmd, 1);
			}
		}
	}

	Clear();
}

void DebugDraw::Clear()
{
	mShapes.Clear();
	mOverlays.Clear();
	bWarnedAboutOverflow = false;
}

void DebugDraw::Destroy()
{
	Clear();

	for (Ref<PrimitiveMesh>& mesh : mMeshes) {
		mesh = nullptr;
	}
}

} // namespace fx::renderer
