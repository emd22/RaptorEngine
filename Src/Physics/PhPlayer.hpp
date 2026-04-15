#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Character/CharacterVirtual.h>

#include <Core/Types.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>

namespace fx {

class PhPlayer
{
public:
    static constexpr float32 scStandingHeight = 1.72f;

public:
    PhPlayer() {}

    void Create();
    void Teleport(const Vec3f& position);
    void ApplyMovement(const Vec3f& direction);

    void Update(float64 delta_time);

public:
    JPH::Ref<JPH::CharacterVirtual> pPlayerVirt;
    JPH::RefConst<JPH::Shape> pPhysicsShape;

    JPH::Vec3 mMovementVector = JPH::Vec3::sZero();

    // float32 HeadRecoveryYOffset = 0.0f;
    float mTime = 0.01f;

    bool bIsGrounded : 1 = false;

    bool bDisableGravity : 1 = false;
};

} // namespace fx
