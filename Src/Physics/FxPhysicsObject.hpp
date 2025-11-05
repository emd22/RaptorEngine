#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/Body.h>
#include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/MemPool/FxMPPagedArray.hpp>
#include <FxEntity.hpp>
#include <FxMaterial.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>

struct FxPhysicsProperties
{
public:
    FxPhysicsProperties() = default;

    float32 ConvexRadius = 0.01f;
    float32 Friction = 0.01f;
    float32 Restitution = 0.3f;

    /**
     * @brief Density of the physics material in kg / m^3
     * @default The density of white pine.
     */
    float32 Density = 350.0f;
};


class FxPhysicsObject
{
public:
    enum PhysicsFlags
    {
        PF_CreateInactive = 0x01,
    };

    enum class PhysicsType
    {
        Static,
        Dynamic,
    };

public:
    void CreatePhysicsBody(const FxVec3f& dimensions, const FxVec3f& initial_position, PhysicsFlags flags,
                           PhysicsType type, const FxPhysicsProperties& properties);
    void DestroyPhysicsBody();

    void Teleport(FxVec3f position, FxQuat rotation);

    FX_FORCE_INLINE FxVec3f GetPosition() { return FxVec3f(mpPhysicsBody->GetPosition()); }
    FX_FORCE_INLINE FxQuat GetRotation() { return FxQuat(mpPhysicsBody->GetRotation()); }

    FX_FORCE_INLINE JPH::Body* GetBody() { return mpPhysicsBody; };
    FX_FORCE_INLINE const JPH::BodyID& GetBodyId() { return mpPhysicsBody->GetID(); };

public:
    JPH::Body* mpPhysicsBody = nullptr;

    bool mbHasPhysicsBody : 1 = false;
};
