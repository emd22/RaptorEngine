#include "FxPhysicsObject.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <FxEngine.hpp>
#include <FxPhysicsJolt.hpp>

void FxPhysicsObject::CreatePhysicsBody(const FxVec3f& dimensions, const FxVec3f& initial_position,
                                        FxPhysicsObject::PhysicsFlags flags, FxPhysicsObject::PhysicsType type,
                                        const FxPhysicsProperties& properties)
{
    if (mbHasPhysicsBody) {
        FxLogWarning("Attempting to create physics body when one is already created!");
        return;
    }


    JPH::EActivation activation_mode = JPH::EActivation::Activate;

    if (flags & FxPhysicsObject::PF_CreateInactive) {
        activation_mode = JPH::EActivation::DontActivate;
    }

    JPH::EMotionType motion_type = JPH::EMotionType::Static;

    FxPhysicsLayer::Type object_layer = FxPhysicsLayer::Static;

    switch (type) {
    case FxPhysicsObject::PhysicsType::Static:
        motion_type = JPH::EMotionType::Static;
        object_layer = FxPhysicsLayer::Static;
        break;
    case FxPhysicsObject::PhysicsType::Dynamic:
        motion_type = JPH::EMotionType::Dynamic;
        object_layer = FxPhysicsLayer::Dynamic;
        break;
    default:
        break;
    }

    JPH::RVec3 box_position;
    initial_position.ToJoltVec3(box_position);

    JPH::RVec3 box_dimensions;
    dimensions.ToJoltVec3(box_dimensions);

    FxLogDebug("Creating physics body of dimensions {}", dimensions);

    JPH::BoxShapeSettings box_shape_settings(box_dimensions);
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


void FxPhysicsObject::DestroyPhysicsBody()
{
    if (!mbHasPhysicsBody && mpPhysicsBody != nullptr) {
        return;
    }

    gPhysics->PhysicsSystem.GetBodyInterface().DestroyBody(GetBodyId());

    mpPhysicsBody = nullptr;
}


void FxPhysicsObject::Teleport(FxVec3f position, FxQuat rotation)
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
