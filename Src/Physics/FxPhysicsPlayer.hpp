#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Character/CharacterVirtual.h>

#include <Core/FxTypes.hpp>
#include <Math/FxQuat.hpp>
#include <Math/FxVec3.hpp>

class FxPhysicsPlayer
{
public:
    static constexpr float32 scStandingHeight = 1.65f;

public:
    FxPhysicsPlayer() {}

    void Create();
    void Teleport(const FxVec3f& position);
    void ApplyMovement(const FxVec3f& direction);

    void Update(float32 delta_time);

public:
    JPH::Ref<JPH::CharacterVirtual> pPlayerVirt;
    JPH::RefConst<JPH::Shape> pPhysicsShape;

    JPH::Vec3 mMovementVector = JPH::Vec3::sZero();

    float mTime = 0.01f;
};
