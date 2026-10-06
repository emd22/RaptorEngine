#include "Body.hpp"

#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"

#include <Engine.hpp>
#include <Renderer/PrimitiveMesh.hpp>


namespace fx {
namespace physics {

const BodyID BodyID::scNull = BodyID(UINT32_MAX);

static RxBodyProps ToRust(const BodyProps& props)
{
	return RxBodyProps { .convex_radius = props.ConvexRadius,
						 .friction = props.Friction,
						 .restitution = props.Restitution,
						 .density = props.Density };
}

static RxPhysicsWorld* GetWorld() { return gPhysics->pBackend->pWorld; }

bool Body::ReportCreateStatus(int32 status, const char* what)
{
	switch (status) {
	case RX_PHYSICS_OK:
		return true;
	case RX_PHYSICS_SHAPE_ERROR:
		LogError(LC_PHYSICS, "Failed to create {}: {}", what, rx_physics_last_error(GetWorld()));
		break;
	case RX_PHYSICS_TOO_FEW_POINTS:
		LogError(LC_PHYSICS, "Cannot create convex hull collider from too few points");
		break;
	case RX_PHYSICS_NOT_TRIANGLES:
		LogError(LC_PHYSICS, "Cannot create mesh collider, the indices are not whole triangles");
		break;
	case RX_PHYSICS_BODY_EXISTS:
		LogWarning(LC_PHYSICS, "Attempting to create physics body when one is already created!");
		break;
	default:
		LogError(LC_PHYSICS, "Failed to create {}, there is no room for another body", what);
		break;
	}

	return false;
}

void Body::CreatePrimitiveBody(ePrimitiveType primitive_type, const Vec3f& dimensions, physics::eMotionType motion_type,
							   const BodyProps& object_properties)
{
	mMotionType = motion_type;
	PrimitiveType = primitive_type;

	constexpr float scMinDim = 0.01f;
	if (dimensions.X <= 0.0f || dimensions.Y <= 0.0f || dimensions.Z <= 0.0f) {
		LogWarning(LC_PHYSICS, "Creating primitive collider with invalid dimensions {} - clamping to minimum {}",
				   dimensions, scMinDim);
	}

	if (primitive_type != ePrimitiveType::Box) {
		return;
	}

	const RxBodyProps props = ToRust(object_properties);

	uint32 body = RX_NO_BODY;
	float32 used[3];

	const int32 status = rx_physics_create_box_body(GetWorld(), mBodyId, &dimensions.mData[0],
													motion_type == physics::eMotionType::Dynamic, &props, &body, used);

	if (!ReportCreateStatus(status, "box collider")) {
		return;
	}

	Dimensions = Vec3f(used[0], used[1], used[2]);

	LogInfo(LC_PHYSICS, "Creating primitive collider with dimensions {}", Dimensions);

	mBodyId = body;
	mbHasPhysicsBody = true;
}

void Body::CreateMeshBody(const PrimitiveMesh& mesh, physics::eMotionType motion_type,
						  const BodyProps& object_properties)
{
	mMotionType = motion_type;

	const std::vector<float32> positions = mesh.GetPositions();
	Assert(!positions.empty());

	size_t index_count = 0;
	const uint32* indices = mesh.GetIndices(index_count);

	const RxBodyProps props = ToRust(object_properties);

	uint32 body = RX_NO_BODY;

	const int32 status = rx_physics_create_mesh_body(
		GetWorld(), mbHasPhysicsBody ? mBodyId : RX_NO_BODY, positions.data(), static_cast<uint32>(positions.size() / 3),
		indices, index_count, motion_type == physics::eMotionType::Dynamic, &props,
		&body);

	if (ReportCreateStatus(status, "mesh collider")) {
		mBodyId = body;
		mbHasPhysicsBody = true;
	}
}

void Body::CreateConvexHullBody(const SizedArray<Vec3f>& points, physics::eMotionType motion_type,
								const BodyProps& object_properties)
{
	mMotionType = motion_type;
	PrimitiveType = ePrimitiveType::None;

	std::vector<float32> flat;
	flat.reserve(static_cast<size_t>(points.Size) * 3);

	for (const Vec3f& point : points) {
		flat.insert(flat.end(), { point.X, point.Y, point.Z });
	}

	const RxBodyProps props = ToRust(object_properties);

	uint32 body = RX_NO_BODY;
	float32 size[3];

	const int32 status = rx_physics_create_hull_body(GetWorld(), mBodyId, flat.data(), points.Size,
													 motion_type == physics::eMotionType::Dynamic, &props, &body, size);

	if (!ReportCreateStatus(status, "convex hull collider")) {
		return;
	}

	Dimensions = Vec3f(size[0], size[1], size[2]);

	mBodyId = body;
	mbHasPhysicsBody = true;
}

void Body::DestroyPhysicsBody()
{
	if (!mbHasPhysicsBody) {
		return;
	}

	rx_physics_destroy_body(GetWorld(), mBodyId);

	mBodyId = RX_NO_BODY;
	mbHasPhysicsBody = false;
}

Vec3f Body::GetPosition() const
{
	float32 position[3], rotation[4];
	rx_physics_position_rotation(GetWorld(), mBodyId, position, rotation);

	return Vec3f(position[0], position[1], position[2]);
}

Quat Body::GetRotation() const
{
	float32 position[3], rotation[4];
	rx_physics_position_rotation(GetWorld(), mBodyId, position, rotation);

	return Quat(rotation[0], rotation[1], rotation[2], rotation[3]);
}

void Body::GetWorldBounds(Vec3f& out_min, Vec3f& out_max) const
{
	if (!mbHasPhysicsBody) {
		out_min = out_max = Vec3f::sZero;
		return;
	}

	rx_physics_body_bounds(GetWorld(), mBodyId, &out_min.mData[0], &out_max.mData[0]);
}

void Body::SetMidpoint(const Vec3f& midpoint) { Midpoint = midpoint; }

void Body::RemoveFromWorld()
{
	if (mbHasPhysicsBody) {
		rx_physics_remove_from_world(GetWorld(), mBodyId);
	}
}

void Body::AddToWorld()
{
	if (mbHasPhysicsBody) {
		rx_physics_add_to_world(GetWorld(), mBodyId);
	}
}

void Body::Teleport(const Vec3f& position, const Quat& rotation)
{
	if (!mbHasPhysicsBody) {
		return;
	}

	const Vec3f target = position + Midpoint;

	rx_physics_teleport(GetWorld(), mBodyId, &target.mData[0], &rotation.mData[0]);
}

} // namespace physics

} // namespace fx
