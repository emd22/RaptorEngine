#include "PhPlayer.hpp"

#include "PhJolt.hpp"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <Asset/ConfigFile.hpp>
#include <Engine.hpp>
#include <Math/MathUtil.hpp>

namespace fx {

static constexpr float32 scMaxSlopeAngle = MathUtil::DegreesToRadians(45.0f);

using namespace JPH;

void PhPlayer::Create()
{
    ConfigFile player_config;
    player_config.Load(FX_BASE_DIR "/Data/Player.conf");

    const float32 collider_radius = player_config.GetEntry(HashStr32("ColliderRadius"))->Get<float32>();

    Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings;

    pPhysicsShape = RotatedTranslatedShapeSettings(Vec3(0, 0.5f * scStandingHeight + collider_radius, 0),
                                                   JPH::Quat::sIdentity(),
                                                   new CapsuleShape(0.5f * scStandingHeight, collider_radius))
                        .Create()
                        .Get();

    settings->mMaxSlopeAngle = scMaxSlopeAngle;
    settings->mShape = pPhysicsShape;

    settings->mMaxStrength = player_config.GetEntry(HashStr32("Strength"))->Get<float32>();
    settings->mMass = player_config.GetEntry(HashStr32("Mass"))->Get<float32>();
    settings->mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings->mSupportingVolume = Plane(Vec3::sAxisY(), -collider_radius);
    settings->mInnerBodyLayer = PhLayer::Dynamic;

    pPlayerVirt = new CharacterVirtual(settings, RVec3::sZero(), JPH::Quat::sIdentity(), 0, &gPhysics->PhysicsSystem);
}

void PhPlayer::Teleport(const Vec3f& position)
{
    JPH::RVec3 jolt_position;
    position.ToJoltVec3(jolt_position);

    pPlayerVirt->SetPosition(jolt_position);
}

void PhPlayer::ApplyMovement(const Vec3f& direction)
{
    Vec3 jolt_dir;
    direction.ToJoltVec3(jolt_dir);

    mMovementVector = jolt_dir;
    // pPlayerVirt->SetLinearVelocity(jolt_dir);
}

SizedArray<JPH::BodyID> PhPlayer::Raycast(Vec3f direction) const
{
    JPH::RayCast rc;
    rc.mOrigin = pPlayerVirt->GetPosition();
    direction.ToJoltVec3(rc.mDirection);

    JPH::AllHitCollisionCollector<RayCastBodyCollector> collector;

    gPhysics->PhysicsSystem.GetBroadPhaseQuery().CastRay(rc, collector);

    LogInfo("Capacity: {}", collector.mHits.size());
    SizedArray<JPH::BodyID> hits;
    hits.InitCapacity(collector.mHits.size());

    for (JPH::BroadPhaseCastResult& hit : collector.mHits) {
        hits.Insert(hit.mBodyID);
    }

    // return {};

    return hits;
}

void PhPlayer::Update(float64 delta_time)
{
    mTime += delta_time;


    PhysicsSystem& phys = gPhysics->PhysicsSystem;

    Vec3 gravity = (phys.GetGravity() * delta_time);

    // Apply gravity
    Vec3 velocity = Vec3::sZero();

    if (pPlayerVirt->GetGroundState() == CharacterVirtual::EGroundState::OnGround) {
        velocity = Vec3::sZero();

        bIsGrounded = true;
    }
    else {
        velocity = pPlayerVirt->GetLinearVelocity() * pPlayerVirt->GetUp();
        if (bDisableGravity) {
            velocity.SetY(0);
        }
        else {
            velocity += gravity;
        }

        bIsGrounded = false;
    }

    velocity += mMovementVector;

    pPlayerVirt->SetLinearVelocity(velocity);

    // Move character
    CharacterVirtual::ExtendedUpdateSettings update_settings {};
    pPlayerVirt->ExtendedUpdate(delta_time, gravity, update_settings,
                                phys.GetDefaultBroadPhaseLayerFilter(PhLayer::Dynamic),
                                phys.GetDefaultLayerFilter(PhLayer::Dynamic), {}, {}, *gPhysics->pTempAllocator);
}

} // namespace fx
