#include "DebugDraw.hpp"

#include <Asset/MeshGen.hpp>
#include <Core/Log.hpp>
#include <Core/RefUtil.hpp>
#include <Object/ObjectLayer.hpp>
#include <Renderer/Backend/Commands.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/Camera.hpp>
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

void DebugDraw::Draw(eDebugShape shape, const Mat4f& world_matrix, Color color)
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

void DebugDraw::Line(const Vec3f& from, const Vec3f& to, Color color)
{
	const Vec3f delta = to - from;

	const Mat4f world_matrix(Vec4f(delta.X, delta.Y, delta.Z, 0.0f), Vec4f(0.0f, 0.0f, 0.0f, 0.0f),
							 Vec4f(0.0f, 0.0f, 0.0f, 0.0f), Vec4f(from.X, from.Y, from.Z, 1.0f));

	Draw(eDebugShape::Line, world_matrix, color);
}

static Mat4f MakeBoxMatrix(const Vec3f& center, const Vec3f& half_extent, const Quat& rotation)
{
	return Mat4f::AsScale(half_extent) * Mat4f::AsRotation(rotation) * Mat4f::AsTranslation(center);
}

void DebugDraw::WireBox(const Vec3f& center, const Vec3f& half_extent, const Quat& rotation, Color color)
{
	WireBox(MakeBoxMatrix(center, half_extent, rotation), color);
}

void DebugDraw::WireBox(const AABB& box, Color color)
{
	WireBox((box.Min + box.Max) * 0.5f, (box.Max - box.Min) * 0.5f, Quat::scIdentity, color);
}

void DebugDraw::SolidBox(const Vec3f& center, const Vec3f& half_extent, const Quat& rotation, Color color)
{
	SolidBox(MakeBoxMatrix(center, half_extent, rotation), color);
}

void DebugDraw::SolidBox(const AABB& box, Color color)
{
	SolidBox((box.Min + box.Max) * 0.5f, (box.Max - box.Min) * 0.5f, Quat::scIdentity, color);
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

void DebugDraw::Render(const CommandBuffer& cmd, const Camera& camera)
{
	if (mShapes.Size > 0) {
		const Mat4f& camera_matrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);

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

				const Mat4f combined_matrix = queued.WorldMatrix * camera_matrix;
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
