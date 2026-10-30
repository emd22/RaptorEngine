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

CharacterConfig CharacterConfig::FromConfig(const ConfigFile& config)
{
	CharacterConfig out;

	out.Radius = ReadOptionalFloat(config, HashStr32("ColliderRadius"), out.Radius);
	out.Height = PhysicsPlayer::scStandingHeight;
	out.Mass = ReadOptionalFloat(config, HashStr32("Mass"), out.Mass);
	out.Strength = ReadOptionalFloat(config, HashStr32("Strength"), out.Strength);
	out.StepHeight = ReadOptionalFloat(config, HashStr32("StepHeight"), PhysicsPlayer::scDefaultStepHeight);

	return out;
}

CharacterConfig CharacterConfig::LoadPlayer()
{
	ConfigFile player_config;
	player_config.Load("RaptorData/Data/Player.conf");

	return FromConfig(player_config);
}

JPH::Ref<CharacterVirtualSettings> CreateCharacterSettings(const CharacterConfig& config)
{
	JPH::Ref<CharacterVirtualSettings> settings = new CharacterVirtualSettings;

	const float32 cylinder_length = std::max(config.Height - 2.0f * config.Radius, 0.01f);

	settings->mShape = RotatedTranslatedShapeSettings(Vec3(0, 0.5f * cylinder_length + config.Radius, 0),
													  JPH::Quat::sIdentity(),
													  new CapsuleShape(0.5f * cylinder_length, config.Radius))
						   .Create()
						   .Get();

	settings->mMaxSlopeAngle = scMaxSlopeAngle;
	settings->mCollisionTolerance = 0.01f;
	settings->mPredictiveContactDistance = 0.2f;

	settings->mMaxStrength = config.Strength;
	settings->mMass = config.Mass;
	settings->mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
	settings->mSupportingVolume = Plane(Vec3::sAxisY(), -config.Radius);
	settings->mInnerBodyLayer = PhLayer::Dynamic;

	return settings;
}

CharacterVirtual::ExtendedUpdateSettings GetCharacterUpdateSettings(const CharacterConfig& config)
{
	return CharacterVirtual::ExtendedUpdateSettings {
		.mStickToFloorStepDown = JPH::Vec3(0.0f, -PhysicsPlayer::scStickToFloorDistance, 0.0f),
		.mWalkStairsStepUp = Vec3(0.0f, config.StepHeight, 0.0f),
		.mWalkStairsMinStepForward = 0.02f,
		.mWalkStairsStepForwardTest = 0.1f,
	};
}

JPH::Vec3 GetCharacterGravity()
{
	return gPhysics->pBackend->PhysicsSystem.GetGravity() * PhysicsPlayer::scGravityScale;
}

/////////////////////////////////////
// Player
/////////////////////////////////////

void PhysicsPlayer::ContactListener::Record(const CharacterVirtual* character, const CharacterVirtual* other,
											JPH::RVec3Arg position, JPH::Vec3Arg normal, bool is_new)
{
	if (!Bumps.IsInited()) {
		Bumps.InitCapacity(scMaxBumps);
	}

	if (Bumps.Size >= scMaxBumps) {
		return;
	}

	CharacterBump bump;
	bump.OtherUserData = other->GetUserData();
	bump.Point.FromJoltVec3(JPH::Vec3(position));
	bump.Normal.FromJoltVec3(normal);
	bump.Velocity.FromJoltVec3(character->GetLinearVelocity());
	bump.OtherVelocity.FromJoltVec3(other->GetLinearVelocity());
	bump.bNew = is_new;

	Bumps.Insert(bump);
}

void PhysicsPlayer::ContactListener::BlockRagdollPush(const JPH::BodyID& body_id, CharacterContactSettings& settings)
{
	// The player is blocked by ragdoll limbs but never shoves them; the bump push on contact is the only push, so
	// walking into an NPC knocks it the same amount however long the player keeps walking
	if (RagdollUserData::Matches(gPhysics->pBackend->GetBodyInterface().GetUserData(body_id))) {
		settings.mCanReceiveImpulses = false;
	}
}

void PhysicsPlayer::ContactListener::OnContactAdded(const CharacterVirtual* inCharacter, const JPH::BodyID& inBodyID2,
													const SubShapeID& inSubShapeID2, RVec3Arg inContactPosition,
													Vec3Arg inContactNormal, CharacterContactSettings& ioSettings)
{
	BlockRagdollPush(inBodyID2, ioSettings);
}

void PhysicsPlayer::ContactListener::OnContactPersisted(const CharacterVirtual* inCharacter,
														const JPH::BodyID& inBodyID2, const SubShapeID& inSubShapeID2,
														RVec3Arg inContactPosition, Vec3Arg inContactNormal,
														CharacterContactSettings& ioSettings)
{
	BlockRagdollPush(inBodyID2, ioSettings);
}

void PhysicsPlayer::ContactListener::OnCharacterContactAdded(const CharacterVirtual* inCharacter,
															 const CharacterVirtual* inOtherCharacter,
															 const SubShapeID& inSubShapeID2,
															 RVec3Arg inContactPosition, Vec3Arg inContactNormal,
															 CharacterContactSettings& ioSettings)
{
	Record(inCharacter, inOtherCharacter, inContactPosition, inContactNormal, true);
}

void PhysicsPlayer::ContactListener::OnCharacterContactPersisted(const CharacterVirtual* inCharacter,
																 const CharacterVirtual* inOtherCharacter,
																 const SubShapeID& inSubShapeID2,
																 RVec3Arg inContactPosition, Vec3Arg inContactNormal,
																 CharacterContactSettings& ioSettings)
{
	Record(inCharacter, inOtherCharacter, inContactPosition, inContactNormal, false);
}

JPH::ObjectLayer PhysicsPlayer::GetCollisionLayer() const
{
	if (bCollisionEnabled) {
		return PhLayer::Dynamic;
	}

	return PhLayer::Deactivated;
}

void PhysicsPlayer::RefreshContacts()
{
	PhysicsSystem& phys = gPhysics->pBackend->PhysicsSystem;
	const JPH::ObjectLayer collision_layer = GetCollisionLayer();

	if (bCollisionEnabled) {
		pPlayerVirt->RefreshContacts(phys.GetDefaultBroadPhaseLayerFilter(collision_layer),
									 CharacterLayerFilter(RagdollCollision), {}, {},
									 *gPhysics->pBackend->pTempAllocator);
		return;
	}

	pPlayerVirt->RefreshContacts(phys.GetDefaultBroadPhaseLayerFilter(collision_layer),
								 phys.GetDefaultLayerFilter(collision_layer), {}, {},
								 *gPhysics->pBackend->pTempAllocator);
}

void PhysicsPlayer::Create()
{
	ConfigFile player_config;
	player_config.Load("RaptorData/Data/Player.conf");

	Config = CharacterConfig::FromConfig(player_config);

	const float32 jump_height = ReadOptionalFloat(player_config, HashStr32("JumpHeight"), scDefaultJumpHeight);
	const float32 gravity = GetCharacterGravity().Length();

	JumpSpeed = ReadOptionalFloat(player_config, HashStr32("JumpSpeed"), std::sqrt(2.0f * gravity * jump_height));

	CoyoteTime = ReadOptionalFloat(player_config, HashStr32("CoyoteTime"), scDefaultCoyoteTime);

	JPH::Ref<CharacterVirtualSettings> settings = CreateCharacterSettings(Config);
	pPhysicsShape = settings->mShape;

	JPH::CharacterVsCharacterCollisionSimple& character_collision = gPhysics->pBackend->CharacterCollision;

	if (pPlayerVirt != nullptr) {
		character_collision.Remove(pPlayerVirt);
	}

	pPlayerVirt = new CharacterVirtual(settings, RVec3::sZero(), JPH::Quat::sIdentity(), 0,
									   &gPhysics->pBackend->PhysicsSystem);

	pPlayerVirt->SetListener(&mContactListener);
	pPlayerVirt->SetCharacterVsCharacterCollision(&character_collision);
	character_collision.Add(pPlayerVirt);
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

	RefreshContacts();
}

void PhysicsPlayer::SetCollisionEnabled(bool value)
{
	bCollisionEnabled = value;

	const JPH::BodyID inner_body_id = pPlayerVirt->GetInnerBodyID();

	if (inner_body_id.IsInvalid()) {
		return;
	}

	gPhysics->pBackend->GetBodyInterface().SetObjectLayer(inner_body_id, GetCollisionLayer());
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

	const Vec3 gravity = GetCharacterGravity();

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

	const JPH::ObjectLayer collision_layer = GetCollisionLayer();
	const CharacterVirtual::ExtendedUpdateSettings update_settings = GetCharacterUpdateSettings(Config);

	mContactListener.Bumps.Clear();

	if (bCollisionEnabled) {
		pPlayerVirt->ExtendedUpdate(
			delta_time, gravity, update_settings, phys.GetDefaultBroadPhaseLayerFilter(collision_layer),
			CharacterLayerFilter(RagdollCollision), {}, {}, *gPhysics->pBackend->pTempAllocator);
		return;
	}

	pPlayerVirt->ExtendedUpdate(
		delta_time, gravity, update_settings, phys.GetDefaultBroadPhaseLayerFilter(collision_layer),
		phys.GetDefaultLayerFilter(collision_layer), {}, {}, *gPhysics->pBackend->pTempAllocator);
}

} // namespace fx::physics
