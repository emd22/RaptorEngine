#include "Body.hpp"

#include "JoltPhysicsBackend.hpp"
#include "PhMesh.hpp"
#include "PhysicsManager.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/MeshShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <Engine.hpp>
#include <Renderer/PrimitiveMesh.hpp>


namespace fx {

namespace physics {
const BodyID BodyID::scNull = BodyID(UINT32_MAX);
}

void physics::Body::CreatePrimitiveBody(ePrimitiveType primitive_type, const Vec3f& dimensions,
										physics::eMotionType motion_type, const BodyProps& object_properties)
{
	mMotionType = motion_type;
	PrimitiveType = primitive_type;

	JPH::RVec3 jolt_dimensions;
	(dimensions * 0.5).ToJoltVec3(jolt_dimensions);

	Dimensions = dimensions;

	LogInfo(LC_PHYSICS, "Creating primitive collider with dimensions {}", dimensions);

	switch (primitive_type) {
	case ePrimitiveType::None:
		break;
	case ePrimitiveType::Box: {
		JPH::BoxShapeSettings box_shape_settings(jolt_dimensions);
		box_shape_settings.SetDensity(object_properties.Density);
		box_shape_settings.mConvexRadius = object_properties.ConvexRadius;

		JPH::ShapeSettings::ShapeResult box_shape_result = box_shape_settings.Create();
		JPH::ShapeRefC box_shape = box_shape_result.Get();

		UpdateJoltBody(box_shape, physics::Body::eFlags::None, motion_type, object_properties);
	} break;
	}
}

void physics::Body::CreateMeshBody(const PrimitiveMesh& mesh, physics::eMotionType motion_type,
								   const BodyProps& object_properties)
{
	mMotionType = motion_type;

	PhMesh physics_mesh(mesh);

	JPH::MeshShapeSettings mesh_settings = physics_mesh.GetShapeSettings();


	JPH::ShapeSettings::ShapeResult mesh_shape_result = mesh_settings.Create();
	JPH::ShapeRefC box_shape = mesh_shape_result.Get();


	CreateJoltBody(box_shape, physics::Body::eFlags::None, motion_type, object_properties);
}

void physics::Body::CreateJoltBody(JPH::ShapeRefC shape, physics::Body::eFlags flags, physics::eMotionType motion_type,
								   const BodyProps& properties)
{
	if (mbHasPhysicsBody) {
		LogWarning(LC_PHYSICS, "Attempting to create physics body when one is already created!");
		return;
	}

	UpdateJoltBody(shape, flags, motion_type, properties);
}


void physics::Body::UpdateJoltBody(JPH::ShapeRefC shape, physics::Body::eFlags flags, physics::eMotionType motion_type,
								   const BodyProps& properties)
{
	JPH::BodyInterface& body_interface = gPhysics->pBackend->PhysicsSystem.GetBodyInterface();

	Vec3f previous_position = Vec3f::sZero;
	Quat previous_rotation = Quat::scIdentity;

	if (mpPhysicsBody) {
		previous_position = GetPosition();
		previous_rotation = GetRotation();

		body_interface.RemoveBody(mpPhysicsBody->GetID());
		body_interface.DestroyBody(mpPhysicsBody->GetID());
	}

	JPH::EMotionType jolt_motion_type = JPH::EMotionType::Static;
	PhLayer::Type object_layer = PhLayer::Static;
	JPH::EActivation activation_mode = JPH::EActivation::Activate;

	if (flags & Body::eFlags::CreateInactive) {
		activation_mode = JPH::EActivation::DontActivate;
	}

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
	body_interface.AddBody(mpPhysicsBody->GetID(), activation_mode);

	mbHasPhysicsBody = true;
}

void physics::Body::DestroyPhysicsBody()
{
	if (!mbHasPhysicsBody || mpPhysicsBody != nullptr) {
		return;
	}

	JPH::BodyInterface& body_interface = gPhysics->pBackend->PhysicsSystem.GetBodyInterface();

	body_interface.RemoveBody(GetBodyId());
	body_interface.DestroyBody(GetBodyId());

	mpPhysicsBody = nullptr;
	mbHasPhysicsBody = false;
}


void physics::Body::Teleport(const Vec3f& position, const Quat& rotation)
{
	if (!mbHasPhysicsBody) {
		return;
	}

	JPH::RVec3 jolt_position;
	JPH::Quat jolt_rotation;

	position.ToJoltVec3(jolt_position);
	rotation.ToJoltQuaternion(jolt_rotation);

	gPhysics->pBackend->PhysicsSystem.GetBodyInterface().SetPositionAndRotation(
		GetBodyId(), jolt_position, jolt_rotation, JPH::EActivation::Activate);
}

} // namespace fx
