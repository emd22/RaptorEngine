#include "PhObject.hpp"

#include "PhJolt.hpp"
#include "PhMesh.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/MeshShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <FxEngine.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>

/*
 Object->CreateBody(PhBodyType::eBox, FxVec3f(1, 2, 3), PhObject::PhysicsType::eStatic);
 */

void PhObject::CreatePrimitiveBody(PhPrimitiveType primitive_type, const FxVec3f& dimensions, PhMotionType motion_type,
                                   const PhProperties& object_properties)
{
    switch (primitive_type) {
    case PhPrimitiveType::eBox: {
        JPH::RVec3 jolt_dimensions;
        dimensions.ToJoltVec3(jolt_dimensions);

        JPH::BoxShapeSettings box_shape_settings(jolt_dimensions);
        box_shape_settings.SetDensity(object_properties.Density);
        box_shape_settings.mConvexRadius = object_properties.ConvexRadius;

        JPH::ShapeSettings::ShapeResult box_shape_result = box_shape_settings.Create();
        JPH::ShapeRefC box_shape = box_shape_result.Get();

        CreateJoltBody(box_shape, {}, motion_type, object_properties);
    }
    }
}

void PhObject::CreateMeshBody(const FxPrimitiveMesh<>& mesh, PhMotionType motion_type,
                              const PhProperties& object_properties)
{
    PhMesh physics_mesh(mesh);

    JPH::ShapeSettings::ShapeResult mesh_shape_result = physics_mesh.GetShapeResult();
    JPH::ShapeRefC box_shape = mesh_shape_result.Get();

    CreateJoltBody(box_shape, {}, motion_type, object_properties);
}


void PhObject::CreateJoltBody(JPH::ShapeRefC shape, PhObject::Flags flags, PhMotionType motion_type,
                              const PhProperties& properties)
{
    if (mbHasPhysicsBody) {
        FxLogWarning("Attempting to create physics body when one is already created!");
        return;
    }


    JPH::EActivation activation_mode = JPH::EActivation::Activate;

    if (flags & PhObject::Flags::eCreateInactive) {
        activation_mode = JPH::EActivation::DontActivate;
    }

    JPH::EMotionType jolt_motion_type = JPH::EMotionType::Static;

    PhLayer::Type object_layer = PhLayer::Static;

    switch (motion_type) {
    case PhMotionType::eStatic:
        jolt_motion_type = JPH::EMotionType::Static;
        object_layer = PhLayer::Static;
        break;
    case PhMotionType::eDynamic:
        jolt_motion_type = JPH::EMotionType::Dynamic;
        object_layer = PhLayer::Dynamic;
        break;
    default:
        break;
    }

    JPH::BodyCreationSettings body_settings(shape, JPH::Vec3::sZero(), JPH::Quat::sIdentity(), jolt_motion_type,
                                            object_layer);

    body_settings.mFriction = properties.Friction;
    body_settings.mRestitution = properties.Restitution;

    JPH::BodyInterface& body_interface = gPhysics->PhysicsSystem.GetBodyInterface();

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
