#include "PhObject.hpp"

#include "PhJolt.hpp"
#include "PhMesh.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/MeshShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <Engine.hpp>
#include <Renderer/PrimitiveMesh.hpp>


namespace fx {

void PhObject::CreatePrimitiveBody(ePhPrimitiveType primitive_type, const Vec3f& dimensions, ePhMotionType motion_type,
                                   const PhProperties& object_properties)
{
    mMotionType = motion_type;

    JPH::RVec3 jolt_dimensions;
    (dimensions * 0.5).ToJoltVec3(jolt_dimensions);

    Dimensions = dimensions;

    LogInfo("Creating primitive collider with dimensions {}", dimensions);

    switch (primitive_type) {
    case ePhPrimitiveType::Box: {
        JPH::BoxShapeSettings box_shape_settings(jolt_dimensions);
        box_shape_settings.SetDensity(object_properties.Density);
        box_shape_settings.mConvexRadius = object_properties.ConvexRadius;

        JPH::ShapeSettings::ShapeResult box_shape_result = box_shape_settings.Create();
        JPH::ShapeRefC box_shape = box_shape_result.Get();

        UpdateJoltBody(box_shape, PhObject::eFlags::None, motion_type, object_properties);
    } break;
    }
}

void PhObject::CreateMeshBody(const PrimitiveMesh& mesh, ePhMotionType motion_type,
                              const PhProperties& object_properties)
{
    mMotionType = motion_type;

    PhMesh physics_mesh(mesh);

    JPH::MeshShapeSettings mesh_settings = physics_mesh.GetShapeSettings();


    JPH::ShapeSettings::ShapeResult mesh_shape_result = mesh_settings.Create();
    JPH::ShapeRefC box_shape = mesh_shape_result.Get();


    CreateJoltBody(box_shape, PhObject::eFlags::None, motion_type, object_properties);
}

void PhObject::CreateJoltBody(JPH::ShapeRefC shape, PhObject::eFlags flags, ePhMotionType motion_type,
                              const PhProperties& properties)
{
    if (mbHasPhysicsBody) {
        LogWarning("Attempting to create physics body when one is already created!");
        return;
    }

    UpdateJoltBody(shape, flags, motion_type, properties);
}


void PhObject::UpdateJoltBody(JPH::ShapeRefC shape, PhObject::eFlags flags, ePhMotionType motion_type,
                              const PhProperties& properties)
{
    JPH::BodyInterface& body_interface = gPhysics->PhysicsSystem.GetBodyInterface();

    Vec3f previous_position = Vec3f::sZero;
    Quat previous_rotation = Quat::sIdentity;

    if (mpPhysicsBody) {
        previous_position = GetPosition();
        previous_rotation = GetRotation();

        body_interface.RemoveBody(mpPhysicsBody->GetID());
        body_interface.DestroyBody(mpPhysicsBody->GetID());
    }

    JPH::EMotionType jolt_motion_type = JPH::EMotionType::Static;
    PhLayer::Type object_layer = PhLayer::Static;
    JPH::EActivation activation_mode = JPH::EActivation::Activate;

    if (flags & PhObject::eFlags::CreateInactive) {
        activation_mode = JPH::EActivation::DontActivate;
    }

    switch (motion_type) {
    case ePhMotionType::Static:
        jolt_motion_type = JPH::EMotionType::Static;
        object_layer = PhLayer::Static;
        break;
    case ePhMotionType::Dynamic:
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

void PhObject::DestroyPhysicsBody()
{
    if (!mbHasPhysicsBody || mpPhysicsBody != nullptr) {
        return;
    }

    JPH::BodyInterface& body_interface = gPhysics->PhysicsSystem.GetBodyInterface();

    body_interface.RemoveBody(GetBodyId());
    body_interface.DestroyBody(GetBodyId());

    mpPhysicsBody = nullptr;
    mbHasPhysicsBody = false;
}


void PhObject::Teleport(Vec3f position, Quat rotation)
{
    if (!mbHasPhysicsBody) {
        return;
    }

    JPH::RVec3 jolt_position;
    JPH::Quat jolt_rotation;

    position.ToJoltVec3(jolt_position);
    rotation.ToJoltQuaternion(jolt_rotation);

    gPhysics->PhysicsSystem.GetBodyInterface().SetPositionAndRotation(GetBodyId(), jolt_position, jolt_rotation,
                                                                      JPH::EActivation::Activate);

    LogInfo("Teleporting to sync physics object");
}

} // namespace fx
