#include "PhysicsPlayer.hpp"

#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"

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

namespace fx::physics {

static constexpr float32 scMaxSlopeAngle = MathUtil::DegreesToRadians(45.0f);

using namespace JPH;

void PhysicsPlayer::Create()
{
	ConfigFile player_config;
	player_config.Load(FX_BASE_DIR "/Data/Player.conf");

	const float32 collider_radius = player_config.GetEntry(HashStr32("ColliderRadius"))->Get<float32>();

	JPH::Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings;

	pPhysicsShape = RotatedTranslatedShapeSettings(Vec3(0, 0.5f * scStandingHeight + collider_radius, 0),
												   JPH::Quat::sIdentity(),
												   new CapsuleShape(0.5f * scStandingHeight, collider_radius))
						.Create()
						.Get();

	settings->mMaxSlopeAngle = scMaxSlopeAngle;
	settings->mShape = pPhysicsShape;
	settings->mCollisionTolerance = 0.01f;
	settings->mPredictiveContactDistance = 0.2f;

	settings->mMaxStrength = player_config.GetEntry(HashStr32("Strength"))->Get<float32>();
	settings->mMass = player_config.GetEntry(HashStr32("Mass"))->Get<float32>();
	settings->mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
	settings->mSupportingVolume = Plane(Vec3::sAxisY(), -collider_radius);
	settings->mInnerBodyLayer = PhLayer::Dynamic;

	pPlayerVirt = new CharacterVirtual(settings, RVec3::sZero(), JPH::Quat::sIdentity(), 0,
									   &gPhysics->pBackend->PhysicsSystem);
}

void PhysicsPlayer::Teleport(const Vec3f& position)
{
	JPH::RVec3 jolt_position;
	position.ToJoltVec3(jolt_position);

	pPlayerVirt->SetPosition(jolt_position);
}

void PhysicsPlayer::SetCollisionEnabled(bool value)
{
	bCollisionEnabled = value;

	gPhysics->pBackend->GetBodyInterface().SetObjectLayer(pPlayerVirt->GetInnerBodyID(), PhLayer::Deactivated);
}

void PhysicsPlayer::ApplyMovement(const Vec3f& direction)
{
	Vec3 jolt_dir;
	direction.ToJoltVec3(jolt_dir);

	mMovementVector = jolt_dir;
	// pPlayerVirt->SetLinearVelocity(jolt_dir);
}

SizedArray<JPH::BodyID> PhysicsPlayer::RaycastBodies(Vec3f direction) const
{
	JPH::RayCast rc;
	rc.mOrigin = pPlayerVirt->GetPosition();
	direction.ToJoltVec3(rc.mDirection);

	JPH::AllHitCollisionCollector<RayCastBodyCollector> collector;

	gPhysics->pBackend->PhysicsSystem.GetBroadPhaseQuery().CastRay(rc, collector);

	SizedArray<JPH::BodyID> hits;
	hits.InitCapacity(collector.mHits.size());

	for (JPH::BroadPhaseCastResult& hit : collector.mHits) {
		hits.Insert(hit.mBodyID);
	}

	return hits;
}


void PhysicsPlayer::Update(float64 delta_time)
{
	mTime += delta_time;

	PhysicsSystem& phys = gPhysics->pBackend->PhysicsSystem;

	Vec3 gravity = (phys.GetGravity() * 1.5f * delta_time);

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

	JPH::ObjectLayer collision_layer = PhLayer::Dynamic;
	if (bCollisionEnabled == false) {
		collision_layer = PhLayer::Deactivated;
	}

	// Move character
	CharacterVirtual::ExtendedUpdateSettings update_settings {
		.mStickToFloorStepDown = JPH::Vec3(0.0f, -0.1f, 0.0f),
		.mWalkStairsStepUp = Vec3(0, 1, 0),
		.mWalkStairsMinStepForward = 0.02f,
		.mWalkStairsStepForwardTest = 0.1f,

	};
	pPlayerVirt->ExtendedUpdate(
		delta_time, gravity, update_settings, phys.GetDefaultBroadPhaseLayerFilter(collision_layer),
		phys.GetDefaultLayerFilter(collision_layer), {}, {}, *gPhysics->pBackend->pTempAllocator);
}

} // namespace fx::physics
