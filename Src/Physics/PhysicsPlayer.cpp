#include "PhysicsPlayer.hpp"

#include "JoltPhysicsBackend.hpp"
#include "PhysicsManager.hpp"

#include <Asset/ConfigFile.hpp>
#include <Engine.hpp>
#include <Math/MathUtil.hpp>

namespace fx::physics {

static constexpr float32 scMaxSlopeAngle = MathUtil::DegreesToRadians(45.0f);

static RxPhysicsWorld* GetWorld() { return gPhysics->pBackend->pWorld; }

void PhysicsPlayer::Create()
{
	ConfigFile player_config;
	player_config.Load("RaptorData/Data/Player.conf");

	const RxCharacterSpec spec = {
		.standing_height = scStandingHeight,
		.radius = player_config.GetEntry(HashStr32("ColliderRadius"))->Get<float32>(),
		.mass = player_config.GetEntry(HashStr32("Mass"))->Get<float32>(),
		.max_strength = player_config.GetEntry(HashStr32("Strength"))->Get<float32>(),
		.max_slope_angle = scMaxSlopeAngle,
	};

	mpCharacter = rx_character_new(GetWorld(), &spec);

	AssertMsg(mpCharacter != nullptr, "Could not create the player's character");
}

void PhysicsPlayer::Teleport(const Vec3f& position) { rx_character_teleport(mpCharacter, GetWorld(), &position.mData[0]); }

void PhysicsPlayer::SetCollisionEnabled(bool value)
{
	rx_character_set_collision_enabled(mpCharacter, GetWorld(), value);
}

void PhysicsPlayer::ApplyMovement(const Vec3f& direction) { rx_character_apply_movement(mpCharacter, &direction.mData[0]); }

void PhysicsPlayer::SetGravityDisabled(bool value)
{
	mbGravityDisabled = value;
	rx_character_set_gravity_disabled(mpCharacter, value);
}

bool PhysicsPlayer::IsGrounded() const { return mpCharacter != nullptr && rx_character_is_grounded(mpCharacter) != 0; }

Vec3f PhysicsPlayer::GetPosition() const
{
	Vec3f position;
	rx_character_position(mpCharacter, GetWorld(), &position.mData[0]);

	return position;
}

Vec3f PhysicsPlayer::GetLinearVelocity() const
{
	Vec3f velocity;
	rx_character_linear_velocity(mpCharacter, GetWorld(), &velocity.mData[0]);

	return velocity;
}

void PhysicsPlayer::Update(float64 delta_time)
{
	rx_character_update(mpCharacter, GetWorld(), static_cast<float32>(delta_time));
}

PhysicsPlayer::~PhysicsPlayer()
{
	if (mpCharacter != nullptr && gPhysics != nullptr && gPhysics->pBackend != nullptr &&
		gPhysics->pBackend->pWorld != nullptr) {
		rx_character_free(mpCharacter, gPhysics->pBackend->pWorld);
	}
}

} // namespace fx::physics
