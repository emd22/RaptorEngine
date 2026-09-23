#include "Body.hpp"

#include "JoltPhysicsBackend.hpp"
#include "PhMesh.hpp"
#include "PhysicsManager.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/MeshShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <Engine.hpp>
#include <Renderer/PrimitiveMesh.hpp>


namespace fx {
namespace physics {

const BodyID BodyID::scNull = BodyID(UINT32_MAX);

void Body::CreatePrimitiveBody(ePrimitiveType primitive_type, const Vec3f& dimensions, physics::eMotionType motion_type,
							   const BodyProps& object_properties)
{
	mMotionType = motion_type;
	PrimitiveType = primitive_type;

	JPH::RVec3 jolt_dimensions;

	constexpr float scMinDim = 0.01f;
	if (dimensions.X <= 0.0f || dimensions.Y <= 0.0f || dimensions.Z <= 0.0f) {
		LogWarning(LC_PHYSICS, "Creating primitive collider with invalid dimensions {} - clamping to minimum {}",
				   dimensions, scMinDim);
	}
	Dimensions = Vec3f(std::max(dimensions.X, scMinDim), std::max(dimensions.Y, scMinDim),
					   std::max(dimensions.Z, scMinDim));

	// Jolt uses half dimensions (i.e. radius vs diameter) so we need to give it half of our shizzle
	(Dimensions * 0.5).ToJoltVec3(jolt_dimensions);

	LogInfo(LC_PHYSICS, "Creating primitive collider with dimensions {}", Dimensions);

	switch (primitive_type) {
	case ePrimitiveType::None:
		break;
	case ePrimitiveType::Box: {
		JPH::BoxShapeSettings box_shape_settings(jolt_dimensions);
		box_shape_settings.SetDensity(object_properties.Density);
		// Jolt refuses a convex radius larger than the smallest half extent
		box_shape_settings.mConvexRadius = std::min(object_properties.ConvexRadius, jolt_dimensions.ReduceMin());

		JPH::ShapeSettings::ShapeResult box_shape_result = box_shape_settings.Create();
		if (box_shape_result.HasError()) {
			LogError(LC_PHYSICS, "Failed to create box collider: {}", box_shape_result.GetError().c_str());
			return;
		}

		JPH::ShapeRefC box_shape = box_shape_result.Get();

		UpdateJoltBody(box_shape, physics::Body::eFlags::None, motion_type, object_properties);
	} break;
	}
}

void Body::CreateMeshBody(const PrimitiveMesh& mesh, physics::eMotionType motion_type,
						  const BodyProps& object_properties)
{
	mMotionType = motion_type;

	PhMesh physics_mesh(mesh);

	JPH::MeshShapeSettings mesh_settings = physics_mesh.GetShapeSettings();


	JPH::ShapeSettings::ShapeResult mesh_shape_result = mesh_settings.Create();
	JPH::ShapeRefC box_shape = mesh_shape_result.Get();


	CreateJoltBody(box_shape, physics::Body::eFlags::None, motion_type, object_properties);
}

void Body::CreateConvexHullBody(const SizedArray<Vec3f>& points, physics::eMotionType motion_type,
								const BodyProps& object_properties)
{
	mMotionType = motion_type;
	PrimitiveType = ePrimitiveType::None;

	if (points.Size < 4) {
		LogError(LC_PHYSICS, "Cannot create convex hull collider from {} points", points.Size);
		return;
	}

	JPH::Array<JPH::Vec3> jolt_points;
	jolt_points.reserve(points.Size);

	Vec3f min = points[0];
	Vec3f max = points[0];

	// TODO: Fix Vec3f::ToJoltVec3 to dupe the Z lane to W
	for (const Vec3f& point : points) {
		jolt_points.push_back(JPH::Vec3(point.X, point.Y, point.Z));

		min = Vec3f::Min(min, point);
		max = Vec3f::Max(max, point);
	}

	Dimensions = max - min;

	// Jolt shrinks the convex radius itself if it is too large for the hull
	JPH::ConvexHullShapeSettings hull_settings(jolt_points, object_properties.ConvexRadius);
	hull_settings.SetDensity(object_properties.Density);

	JPH::ShapeSettings::ShapeResult hull_shape_result = hull_settings.Create();
	if (hull_shape_result.HasError()) {
		LogError(LC_PHYSICS, "Failed to create convex hull collider: {}", hull_shape_result.GetError().c_str());
		return;
	}

	UpdateJoltBody(hull_shape_result.Get(), physics::Body::eFlags::None, motion_type, object_properties);
}

void Body::CreateJoltBody(JPH::ShapeRefC shape, physics::Body::eFlags flags, physics::eMotionType motion_type,
						  const BodyProps& properties)
{
	if (mbHasPhysicsBody) {
		LogWarning(LC_PHYSICS, "Attempting to create physics body when one is already created!");
		return;
	}

	UpdateJoltBody(shape, flags, motion_type, properties);
}


void Body::UpdateJoltBody(JPH::ShapeRefC shape, physics::Body::eFlags flags, physics::eMotionType motion_type,
						  const BodyProps& properties)
{
	JPH::BodyInterface& body_interface = gPhysics->pBackend->PhysicsSystem.GetBodyInterface();

	Vec3f previous_position = Vec3f::sZero;
	Quat previous_rotation = Quat::scIdentity;

	if (mpPhysicsBody) {
		previous_position = GetPosition();
		previous_rotation = GetRotation();

		RemoveFromWorld();
		body_interface.DestroyBody(mpPhysicsBody->GetID());
	}

	JPH::EMotionType jolt_motion_type = JPH::EMotionType::Static;
	PhLayer::Type object_layer = PhLayer::Static;

	switch (motion_type) {
	case physics::eMotionType::Static:
		jolt_motion_type = JPH::EMotionType::Static;
		object_layer = PhLayer::Static;
		break;
	case physics::eMotionType::Dynamic:
		jolt_motion_type = JPH::EMotionType::Dynamic;
		object_layer = PhLayer::Dynamic;
		break;
	default:
		break;
	}

	JPH::RVec3 start_position;
	previous_position.ToJoltVec3(start_position);

	JPH::Quat start_rotation;
	previous_rotation.ToJoltQuaternion(start_rotation);

	JPH::BodyCreationSettings body_settings(shape, start_position, start_rotation, jolt_motion_type, object_layer);

	body_settings.mFriction = properties.Friction;
	body_settings.mRestitution = properties.Restitution;

	mpPhysicsBody = body_interface.CreateBody(body_settings);
	AddToWorld();

	mbHasPhysicsBody = true;
}

void Body::DestroyPhysicsBody()
{
	if (!mbHasPhysicsBody || mpPhysicsBody == nullptr) {
		return;
	}

	JPH::BodyInterface& body_interface = gPhysics->pBackend->PhysicsSystem.GetBodyInterface();

	RemoveFromWorld();
	body_interface.DestroyBody(GetBodyID());

	mpPhysicsBody = nullptr;
	mbHasPhysicsBody = false;
}


void Body::SetMidpoint(const Vec3f& midpoint) { Midpoint = midpoint; }

void Body::RemoveFromWorld()
{
	if (!mbIsInWorld) {
		return;
	}

	gPhysics->pBackend->GetBodyInterface().RemoveBody(mpPhysicsBody->GetID());
	mbIsInWorld = false;
}
void Body::AddToWorld()
{
	if (mbIsInWorld) {
		return;
	}

	gPhysics->pBackend->GetBodyInterface().AddBody(mpPhysicsBody->GetID(), JPH::EActivation::DontActivate);

	mbIsInWorld = true;
}

void Body::Teleport(const Vec3f& position, const Quat& rotation)
{
	if (!mbHasPhysicsBody) {
		return;
	}

	JPH::RVec3 jolt_position;
	JPH::Quat jolt_rotation;

	(position + Midpoint).ToJoltVec3(jolt_position);
	rotation.ToJoltQuaternion(jolt_rotation);

	gPhysics->pBackend->PhysicsSystem.GetBodyInterface().SetPositionAndRotation(
		GetBodyID(), jolt_position, jolt_rotation, JPH::EActivation::Activate);
}

} // namespace physics

} // namespace fx
