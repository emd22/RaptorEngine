#include "PhObject.hpp"

#include "PhJolt.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <FxEngine.hpp>

void PhObject::CreatePhysicsBody(const FxVec3f& dimensions, const FxVec3f& initial_position,
                                 PhObject::PhysicsFlags flags, PhObject::PhysicsType type,
                                 const PhProperties& properties)
{
    if (mbHasPhysicsBody) {
        FxLogWarning("Attempting to create physics body when one is already created!");
        return;
    }


    JPH::EActivation activation_mode = JPH::EActivation::Activate;

    if (flags & PhObject::PF_CreateInactive) {
        activation_mode = JPH::EActivation::DontActivate;
    }

    JPH::EMotionType motion_type = JPH::EMotionType::Static;

    PhLayer::Type object_layer = PhLayer::Static;

    switch (type) {
    case PhObject::PhysicsType::Static:
        motion_type = JPH::EMotionType::Static;
        object_layer = PhLayer::Static;
        break;
    case PhObject::PhysicsType::Dynamic:
        motion_type = JPH::EMotionType::Dynamic;
        object_layer = PhLayer::Dynamic;
        break;
    default:
        break;
    }

    JPH::RVec3 box_position;
    initial_position.ToJoltVec3(box_position);

    JPH::RVec3 box_dimensions;
    dimensions.ToJoltVec3(box_dimensions);

    FxLogDebug("Creating physics body of dimensions {}", dimensions);

    // JPH::RefConst<JPH::PhysicsMaterial> material = new JPH::PhysicsMaterial;

    JPH::BoxShapeSettings box_shape_settings(box_dimensions);
    // box_shape_settings.mMaterial =
    box_shape_settings.SetDensity(properties.Density);
    box_shape_settings.mConvexRadius = properties.ConvexRadius;

    // box_shape_settings.SetEmbedded();

    JPH::ShapeSettings::ShapeResult box_shape_result = box_shape_settings.Create();
    JPH::ShapeRefC box_shape = box_shape_result.Get();

    JPH::BodyCreationSettings body_settings(box_shape, box_position, JPH::Quat::sIdentity(), motion_type, object_layer);
    body_settings.mFriction = properties.Friction;
    body_settings.mRestitution = properties.Restitution;

    JPH::BodyInterface& body_interface = gPhysics->PhysicsSystem.GetBodyInterface();

    mpPhysicsBody = body_interface.CreateBody(body_settings);

    body_interface.AddBody(mpPhysicsBody->GetID(), activation_mode);

    mbHasPhysicsBody = true;

    // body_interface.CreateAndAddBody(body_settings, activation_mode);
}


void PhObject::DestroyPhysicsBody()
{
    if (!mbHasPhysicsBody || mpPhysicsBody != nullptr) {
        return;
    }

    JPH::BodyInterface& body_interface = gPhysics->PhysicsSystem.GetBodyInterface();

    body_interface.RemoveBody(GetBodyId());
    // body_interface.DestroyBody(GetBodyId());

    mpPhysicsBody = nullptr;
    mbHasPhysicsBody = false;
}


void PhObject::Teleport(FxVec3f position, FxQuat rotation)
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
}
