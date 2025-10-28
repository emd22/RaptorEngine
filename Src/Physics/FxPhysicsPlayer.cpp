#include "FxPhysicsPlayer.hpp"

#include "FxPhysicsJolt.hpp"

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <FxEngine.hpp>
#include <Math/FxMathUtil.hpp>


static constexpr float32 scMaxSlopeAngle = FxMath::DegreesToRadians(45.0f);
static constexpr float32 scPlayerRadius = 0.3f;

using namespace JPH;

void FxPhysicsPlayer::Create()
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
    settings->mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings->mSupportingVolume = Plane(Vec3::sAxisY(), -scPlayerRadius);
    settings->mInnerBodyLayer = FxPhysicsLayer::Dynamic;

    pPlayerVirt = new CharacterVirtual(settings, RVec3::sZero(), Quat::sIdentity(), 0, &gPhysics->PhysicsSystem);
}

void FxPhysicsPlayer::Teleport(const FxVec3f& position)
{
    JPH::RVec3 jolt_position;
    position.ToJoltVec3(jolt_position);

    pPlayerVirt->SetPosition(jolt_position);
}

void FxPhysicsPlayer::ApplyMovement(const FxVec3f& by)
{
    Vec3 jolt_dir;
    by.ToJoltVec3(jolt_dir);
    mMovementVector = jolt_dir;
    // pPlayerVirt->SetLinearVelocity(jolt_dir);
}

void FxPhysicsPlayer::Update(float32 delta_time)
{
    mTime += delta_time;

    CharacterVirtual::ExtendedUpdateSettings settings {};

    PhysicsSystem& phys = gPhysics->PhysicsSystem;

    // Apply gravity
    Vec3 velocity;

    if (pPlayerVirt->GetGroundState() == CharacterVirtual::EGroundState::OnGround) {
        velocity = Vec3::sZero();
    }
    else {
        velocity = pPlayerVirt->GetLinearVelocity() * pPlayerVirt->GetUp() + phys.GetGravity() * gPhysics->cDeltaTime;
    }

    velocity += mMovementVector;

    pPlayerVirt->SetLinearVelocity(velocity);

    // Move character
    CharacterVirtual::ExtendedUpdateSettings update_settings;
    // character->ExtendedUpdate(inParams.mDeltaTime, mPhysicsSystem->GetGravity(), update_settings,
    //                           mPhysicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
    //                           mPhysicsSystem->GetDefaultLayerFilter(Layers::MOVING), {}, {}, *mTempAllocator);

    pPlayerVirt->ExtendedUpdate(gPhysics->cDeltaTime, phys.GetGravity(), settings,
                                phys.GetDefaultBroadPhaseLayerFilter(FxPhysicsLayer::Dynamic),
                                phys.GetDefaultLayerFilter(FxPhysicsLayer::Dynamic), {}, {}, *gPhysics->pTempAllocator);
}
