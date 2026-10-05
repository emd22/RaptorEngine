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

static_assert(static_cast<uint32>(eDebugShape::Count) == 3);

} // namespace

DebugDraw::DebugDraw() { mpQueue = rx_debug_draw_new(); }

DebugDraw::~DebugDraw()
{
	Destroy();

	rx_debug_draw_free(mpQueue);
	mpQueue = nullptr;
}

/// The first shape that does not fit in the queue is reported, and the rest are dropped without a word until the
/// queue is drawn
static void ReportQueued(int32 queued)
{
	if (queued == 1) {
		LogWarning("Debug draw queue is full ({} shapes), dropping the rest until it is drawn",
				   DebugDraw::scMaxShapes);
	}
}

void DebugDraw::Draw(eDebugShape shape, const Mat4f& world_matrix, Color color)
{
	ReportQueued(rx_debug_draw_shape(mpQueue, static_cast<uint32>(shape), world_matrix.RawData, color.AsUInt()));
}

void DebugDraw::Line(const Vec3f& from, const Vec3f& to, Color color)
{
	const float from_values[3] = { from.X, from.Y, from.Z };
	const float to_values[3] = { to.X, to.Y, to.Z };

	ReportQueued(rx_debug_draw_line(mpQueue, from_values, to_values, color.AsUInt()));
}

static int32 QueueBox(RxDebugDraw* queue, eDebugShape shape, const Vec3f& center, const Vec3f& half_extent,
					  const Quat& rotation, Color color)
{
	const float center_values[3] = { center.X, center.Y, center.Z };
	const float extent_values[3] = { half_extent.X, half_extent.Y, half_extent.Z };
	const float rotation_values[4] = { rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW() };

	return rx_debug_draw_box(queue, static_cast<uint32>(shape), center_values, extent_values, rotation_values,
							 color.AsUInt());
}

static int32 QueueAABB(RxDebugDraw* queue, eDebugShape shape, const AABB& box, Color color)
{
	const float min_values[3] = { box.Min.X, box.Min.Y, box.Min.Z };
	const float max_values[3] = { box.Max.X, box.Max.Y, box.Max.Z };

	return rx_debug_draw_aabb(queue, static_cast<uint32>(shape), min_values, max_values, color.AsUInt());
}

void DebugDraw::WireBox(const Vec3f& center, const Vec3f& half_extent, const Quat& rotation, Color color)
{
	ReportQueued(QueueBox(mpQueue, eDebugShape::WireBox, center, half_extent, rotation, color));
}

void DebugDraw::WireBox(const AABB& box, Color color)
{
	ReportQueued(QueueAABB(mpQueue, eDebugShape::WireBox, box, color));
}

void DebugDraw::SolidBox(const Vec3f& center, const Vec3f& half_extent, const Quat& rotation, Color color)
{
	ReportQueued(QueueBox(mpQueue, eDebugShape::SolidBox, center, half_extent, rotation, color));
}

void DebugDraw::SolidBox(const AABB& box, Color color)
{
	ReportQueued(QueueAABB(mpQueue, eDebugShape::SolidBox, box, color));
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
	if (rx_debug_draw_count(mpQueue) > 0) {
		const Mat4f& camera_matrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);

		const RxDebugDrawCommand* commands = nullptr;
		size_t command_count = 0;

		rx_debug_draw_commands(mpQueue, camera_matrix.RawData, &commands, &command_count);

		DebugLayerPushConstants push_constants {};

		Pipeline* pipeline = nullptr;
		PrimitiveMesh* mesh = nullptr;
		uint32 bound_shape = UINT32_MAX;

		// The commands come grouped by shape, so a pipeline and mesh are bound once per shape kind instead of once
		// per shape
		for (size_t i = 0; i < command_count; i++) {
			const RxDebugDrawCommand& command = commands[i];

			if (command.shape != bound_shape) {
				const eDebugShape shape = static_cast<eDebugShape>(command.shape);

				pipeline = &gPipelineCache->Request(GetShapePipeline(shape));
				pipeline->Bind(cmd);

				mesh = &GetMesh(shape);
				bound_shape = command.shape;
			}

			static_assert(sizeof(push_constants.CombinedMatrix) == sizeof(command.combined_matrix));

			memcpy(push_constants.CombinedMatrix, command.combined_matrix, sizeof(push_constants.CombinedMatrix));
			push_constants.DebugColor = command.color;

			gGraphics->SubmitPushConstants(cmd, *pipeline, eShaderType::Vertex, push_constants);
			mesh->Render(cmd, 1);
		}
	}

	Clear();
}

void DebugDraw::Clear() { rx_debug_draw_clear(mpQueue); }

void DebugDraw::Destroy()
{
	Clear();

	for (Ref<PrimitiveMesh>& mesh : mMeshes) {
		mesh = nullptr;
	}
}

} // namespace fx::renderer
