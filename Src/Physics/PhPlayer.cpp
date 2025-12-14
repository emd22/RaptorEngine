#include "PhPlayer.hpp"

#include "PhJolt.hpp"

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <FxEngine.hpp>
#include <Math/FxMathUtil.hpp>


static constexpr float32 scMaxSlopeAngle = FxMath::DegreesToRadians(45.0f);
static constexpr float32 scPlayerRadius = 0.3f;

static constexpr float32 scMaxHeadRecovery = 10.0f;

using namespace JPH;

void PhPlayer::Create()
{
    Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings;

    pPhysicsShape = RotatedTranslatedShapeSettings(Vec3(0, 0.5f * scStandingHeight + scPlayerRadius, 0),
                                                   Quat::sIdentity(),
                                                   new CapsuleShape(0.5f * scStandingHeight, scPlayerRadius))
                        .Create()
                        .Get();

    settings->mMaxSlopeAngle = scMaxSlopeAngle;
    settings->mShape = pPhysicsShape;

    settings->mMaxStrength = 100.0f;
    settings->mMass = 90.0f;
    settings->mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings->mSupportingVolume = Plane(Vec3::sAxisY(), -scPlayerRadius);
    settings->mInnerBodyLayer = PhLayer::Dynamic;

    pPlayerVirt = new CharacterVirtual(settings, RVec3::sZero(), Quat::sIdentity(), 0, &gPhysics->PhysicsSystem);
}

void PhPlayer::Teleport(const FxVec3f& position)
{
    JPH::RVec3 jolt_position;
    position.ToJoltVec3(jolt_position);

    pPlayerVirt->SetPosition(jolt_position);
}

void PhPlayer::ApplyMovement(const FxVec3f& by)
{
    Vec3 jolt_dir;
    by.ToJoltVec3(jolt_dir);

    mMovementVector = jolt_dir;
    // pPlayerVirt->SetLinearVelocity(jolt_dir);
}

void PhPlayer::Update(float32 delta_time)
{
    mTime += delta_time;

    CharacterVirtual::ExtendedUpdateSettings settings {};

    PhysicsSystem& phys = gPhysics->PhysicsSystem;

    // Apply gravity
    Vec3 velocity;

    if (pPlayerVirt->GetGroundState() == CharacterVirtual::EGroundState::OnGround) {
        if (!bIsGrounded) {
            HeadRecoveryYOffset = min(-velocity.GetY() * 10.0f, scMaxHeadRecovery);
        }

        velocity = Vec3::sZero();

        bIsGrounded = true;
    }
    else {
        velocity = pPlayerVirt->GetLinearVelocity() * pPlayerVirt->GetUp() +
                   phys.GetGravity() * 0.99 * gPhysics->cDeltaTime;

        bIsGrounded = false;
    }

    if (abs(HeadRecoveryYOffset) > 1e-5) {
        HeadRecoveryYOffset = std::lerp(HeadRecoveryYOffset, 0.0, 0.001 * delta_time);
    }
    else {
        HeadRecoveryYOffset = 0.0f;
    }

    velocity += mMovementVector;

    pPlayerVirt->SetLinearVelocity(velocity);

    // Move character
    CharacterVirtual::ExtendedUpdateSettings update_settings;
    // character->ExtendedUpdate(inParams.mDeltaTime, mPhysicsSystem->GetGravity(), update_settings,
    //                           mPhysicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
    //                           mPhysicsSystem->GetDefaultLayerFilter(Layers::MOVING), {}, {}, *mTempAllocator);

    pPlayerVirt->ExtendedUpdate(gPhysics->cDeltaTime, phys.GetGravity(), settings,
                                phys.GetDefaultBroadPhaseLayerFilter(PhLayer::Dynamic),
                                phys.GetDefaultLayerFilter(PhLayer::Dynamic), {}, {}, *gPhysics->pTempAllocator);
}
