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

static float32 ReadOptionalFloat(const ConfigFile& file, Hash32 name, float32 fallback)
{
	const ConfigEntry* entry = file.GetEntry(name);

	return entry != nullptr ? entry->Get<float32>() : fallback;
}

static JPH::ObjectLayer CollisionLayerFor(bool collision_enabled)
{
	return collision_enabled ? PhLayer::Dynamic : PhLayer::Deactivated;
}

void PhysicsPlayer::Create()
{
	ConfigFile player_config;
	player_config.Load("RaptorData/Data/Player.conf");

	const float32 collider_radius = player_config.GetEntry(HashStr32("ColliderRadius"))->Get<float32>();

	StepHeight = ReadOptionalFloat(player_config, HashStr32("StepHeight"), scDefaultStepHeight);

	const float32 jump_height = ReadOptionalFloat(player_config, HashStr32("JumpHeight"), scDefaultJumpHeight);
	const float32 gravity = (gPhysics->pBackend->PhysicsSystem.GetGravity() * scGravityScale).Length();

	JumpSpeed = ReadOptionalFloat(player_config, HashStr32("JumpSpeed"), std::sqrt(2.0f * gravity * jump_height));

	CoyoteTime = ReadOptionalFloat(player_config, HashStr32("CoyoteTime"), scDefaultCoyoteTime);

	JPH::Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings;

	const float32 cylinder_length = std::max(scStandingHeight - 2.0f * collider_radius, 0.01f);

	pPhysicsShape = RotatedTranslatedShapeSettings(Vec3(0, 0.5f * cylinder_length + collider_radius, 0),
												   JPH::Quat::sIdentity(),
												   new CapsuleShape(0.5f * cylinder_length, collider_radius))
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

void PhysicsPlayer::Teleport(const Vec3f position)
{
	JPH::RVec3 jolt_position;
	position.ToJoltVec3(jolt_position);

	pPlayerVirt->SetPosition(jolt_position);
	pPlayerVirt->SetLinearVelocity(Vec3::sZero());
	mMovementVector = Vec3::sZero();

	mbJumpPending = false;
	mbJumpConsumed = false;
	mTimeSinceGrounded = CoyoteTime + 1.0f;

	PhysicsSystem& phys = gPhysics->pBackend->PhysicsSystem;
	const JPH::ObjectLayer collision_layer = CollisionLayerFor(bCollisionEnabled);

	pPlayerVirt->RefreshContacts(phys.GetDefaultBroadPhaseLayerFilter(collision_layer),
								 phys.GetDefaultLayerFilter(collision_layer), {}, {},
								 *gPhysics->pBackend->pTempAllocator);
}

void PhysicsPlayer::SetCollisionEnabled(bool value)
{
	bCollisionEnabled = value;

	const JPH::BodyID inner_body_id = pPlayerVirt->GetInnerBodyID();

	if (inner_body_id.IsInvalid()) {
		return;
	}

	gPhysics->pBackend->GetBodyInterface().SetObjectLayer(inner_body_id, CollisionLayerFor(value));
}

void PhysicsPlayer::ApplyMovement(const Vec3f direction)
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

	const Vec3 gravity = phys.GetGravity() * scGravityScale;

	// Apply gravity
	Vec3 velocity = Vec3::sZero();

	const bool on_ground = pPlayerVirt->GetGroundState() == CharacterVirtual::EGroundState::OnGround;

	if (on_ground) {
		velocity = pPlayerVirt->GetGroundVelocity();

		mTimeSinceGrounded = 0.0f;
		mbJumpConsumed = false;
	}
	else {
		velocity = pPlayerVirt->GetLinearVelocity() * pPlayerVirt->GetUp();
		if (bDisableGravity) {
			velocity.SetY(0);
		}
		else {
			velocity += gravity * static_cast<float32>(delta_time);
		}

		mTimeSinceGrounded += static_cast<float32>(delta_time);
	}

	bIsGrounded = on_ground;

	if (mbJumpPending) {
		mbJumpPending = false;
		mbJumpConsumed = true;

		const float32 base_velocity = on_ground ? velocity.GetY() : 0.0f;
		velocity.SetY(base_velocity + JumpSpeed);
	}

	velocity += mMovementVector;

	pPlayerVirt->SetLinearVelocity(velocity);

	const JPH::ObjectLayer collision_layer = CollisionLayerFor(bCollisionEnabled);

	// Move character
	CharacterVirtual::ExtendedUpdateSettings update_settings {
		.mStickToFloorStepDown = JPH::Vec3(0.0f, -scStickToFloorDistance, 0.0f),
		.mWalkStairsStepUp = Vec3(0.0f, StepHeight, 0.0f),
		.mWalkStairsMinStepForward = 0.02f,
		.mWalkStairsStepForwardTest = 0.1f,

	};
	pPlayerVirt->ExtendedUpdate(
		delta_time, gravity, update_settings, phys.GetDefaultBroadPhaseLayerFilter(collision_layer),
		phys.GetDefaultLayerFilter(collision_layer), {}, {}, *gPhysics->pBackend->pTempAllocator);
}

} // namespace fx::physics
