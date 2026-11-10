#include "NPCMover.hpp"

#include <Engine.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>

namespace fx {

using namespace physics;

namespace {

/// NPC capsules collide with the world and props, never with ragdoll bodies (their own included)
const CharacterLayerFilter scNPCLayerFilter(eCharacterRagdollCollision::None);

} // namespace

bool NPCMover::Create(const CharacterConfig& config, const Vec3f position, uint32 serial)
{
	Destroy();

	mConfig = config;

	const JPH::RVec3 jolt_position = Vec3f::ToJolt(position);

	JPH::Ref<JPH::CharacterVirtualSettings> settings = CreateCharacterSettings(config);

	mpCharacter = new JPH::CharacterVirtual(settings, jolt_position, JPH::Quat::sIdentity(),
											NPCCharacterUserData::scTag | serial, &gPhysics->pBackend->PhysicsSystem);

	mpCharacter->SetCharacterVsCharacterCollision(&gPhysics->pBackend->CharacterCollision);

	SetEnabled(true);
	RefreshContacts();

	return true;
}

void NPCMover::Destroy()
{
	if (mpCharacter == nullptr) {
		return;
	}

	SetEnabled(false);
	mpCharacter = nullptr;
}

void NPCMover::SetEnabled(bool enabled)
{
	if (mpCharacter == nullptr || mbEnabled == enabled) {
		return;
	}

	mbEnabled = enabled;
	mRestTime = 0.0f;

	JPH::CharacterVsCharacterCollisionSimple& character_collision = gPhysics->pBackend->CharacterCollision;

	if (enabled) {
		character_collision.Add(mpCharacter);
		mpCharacter->SetLinearVelocity(JPH::Vec3::sZero());
		RefreshContacts();
	}
	else {
		character_collision.Remove(mpCharacter);
	}
}

void NPCMover::Teleport(const Vec3f position)
{
	if (mpCharacter == nullptr) {
		return;
	}

	mpCharacter->SetPosition(Vec3f::ToJolt(position));
	mpCharacter->SetLinearVelocity(JPH::Vec3::sZero());
	mRestTime = 0.0f;

	RefreshContacts();
}

void NPCMover::RefreshContacts()
{
	JPH::PhysicsSystem& phys = gPhysics->pBackend->PhysicsSystem;

	mpCharacter->RefreshContacts(phys.GetDefaultBroadPhaseLayerFilter(PhLayer::Dynamic), scNPCLayerFilter, {}, {},
								 *gPhysics->pBackend->pTempAllocator);
}

void NPCMover::Update(float32 delta_time, const Vec3f desired_velocity)
{
	constexpr float32 scMaxStepTime = 0.1f;
	constexpr float32 scRestSpeed = 0.01f;
	// A resting capsule still refreshes this often, so it notices the floor going away
	constexpr float32 scRestRefreshTime = 0.5f;

	if (mpCharacter == nullptr || !mbEnabled || delta_time <= 0.0f) {
		return;
	}

	delta_time = std::min(delta_time, scMaxStepTime);

	const JPH::Vec3 gravity = GetCharacterGravity();
	const bool on_ground = mpCharacter->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

	JPH::Vec3 velocity;

	if (on_ground) {
		velocity = mpCharacter->GetGroundVelocity();
	}
	else {
		velocity = mpCharacter->GetLinearVelocity() * mpCharacter->GetUp();
		velocity += gravity * delta_time;
	}

	velocity += JPH::Vec3(desired_velocity.X, 0.0f, desired_velocity.Z);

	// A capsule standing still on still ground does not need its shape casts every frame
	const bool is_still = on_ground && velocity.LengthSq() < scRestSpeed * scRestSpeed;

	if (is_still) {
		mRestTime += delta_time;
	}
	else {
		mRestTime = 0.0f;
	}

	if (IsResting()) {
		mRefreshTime += delta_time;

		if (mRefreshTime < scRestRefreshTime) {
			mpCharacter->SetLinearVelocity(JPH::Vec3::sZero());
			return;
		}
	}

	mRefreshTime = 0.0f;

	mpCharacter->SetLinearVelocity(velocity);

	JPH::PhysicsSystem& phys = gPhysics->pBackend->PhysicsSystem;

	mpCharacter->ExtendedUpdate(delta_time, gravity, GetCharacterUpdateSettings(mConfig),
								phys.GetDefaultBroadPhaseLayerFilter(PhLayer::Dynamic), scNPCLayerFilter, {}, {},
								*gPhysics->pBackend->pTempAllocator);
}

Vec3f NPCMover::GetPosition() const
{
	if (mpCharacter == nullptr) {
		return Vec3f::sZero;
	}

	return Vec3f::FromJolt(mpCharacter->GetPosition());
}

Vec3f NPCMover::GetVelocity() const
{
	if (mpCharacter == nullptr) {
		return Vec3f::sZero;
	}

	return Vec3f::FromJolt(mpCharacter->GetLinearVelocity());
}

bool NPCMover::IsGrounded() const
{
	return mpCharacter != nullptr && mpCharacter->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

} // namespace fx
