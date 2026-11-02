#pragma once

#include "JoltPhysicsBackend.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Character/CharacterVirtual.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>

namespace fx {
class ConfigFile;
}

namespace fx::physics {

/// Capsule and movement settings shared by the player and NPC characters
struct CharacterConfig
{
	float32 Radius = 0.3f;
	float32 Height = 1.84f;
	float32 Mass = 75.0f;
	float32 Strength = 150.0f;
	float32 StepHeight = 0.4f;

	static CharacterConfig FromConfig(const ConfigFile& config);
	/// Reads Player.conf, so NPCs move with the same capsule as the player
	static CharacterConfig LoadPlayer();
};

JPH::Ref<JPH::CharacterVirtualSettings> CreateCharacterSettings(const CharacterConfig& config);
JPH::CharacterVirtual::ExtendedUpdateSettings GetCharacterUpdateSettings(const CharacterConfig& config);
JPH::Vec3 GetCharacterGravity();

/// The player capsule touching another character during its last update
struct CharacterBump
{
	uint64 OtherUserData = 0;
	Vec3f Point = Vec3f::sZero;
	/// Points from the player into the other character
	Vec3f Normal = Vec3f::sZero;
	Vec3f Velocity = Vec3f::sZero;
	Vec3f OtherVelocity = Vec3f::sZero;
	bool bNew = false;

	/// Speed the player moves into the other character along the contact normal
	FX_FORCE_INLINE float32 GetClosingSpeed() const { return (Velocity - OtherVelocity).Dot(Normal); }
};

class PhysicsPlayer
{
public:
	static constexpr float32 scEyeHeight = 1.72f;
	static constexpr float32 scStandingHeight = 1.84f;

	static constexpr float32 scGravityScale = 1.5f;
	static constexpr float32 scMaxStepTime = 0.1f;
	static constexpr float32 scStickToFloorDistance = 0.5f;

	static constexpr float32 scDefaultStepHeight = 0.4f;
	static constexpr float32 scDefaultJumpHeight = 0.85f;
	static constexpr float32 scDefaultCoyoteTime = 0.1f;

public:
	PhysicsPlayer() {}

	void Create();
	void Teleport(const Vec3f position);
	void ApplyMovement(const Vec3f direction);

	bool CanJump() const { return !bDisableGravity && !mbJumpConsumed && mTimeSinceGrounded <= CoyoteTime; }

	void Jump()
	{
		if (CanJump()) {
			mbJumpPending = true;
		}
	}

	SizedArray<JPH::BodyID> RaycastBodies(Vec3f direction) const;

	FX_FORCE_INLINE const SizedArray<CharacterBump>& GetBumps() const { return mContactListener.Bumps; }

	void SetCollisionEnabled(bool value);

	void Update(float64 delta_time);

private:
	class ContactListener final : public JPH::CharacterContactListener
	{
	public:
		static constexpr uint32 scMaxBumps = 8;

		void OnContactAdded(const JPH::CharacterVirtual* inCharacter, const JPH::BodyID& inBodyID2,
							const JPH::SubShapeID& inSubShapeID2, JPH::RVec3Arg inContactPosition,
							JPH::Vec3Arg inContactNormal, JPH::CharacterContactSettings& ioSettings) override;
		void OnContactPersisted(const JPH::CharacterVirtual* inCharacter, const JPH::BodyID& inBodyID2,
								const JPH::SubShapeID& inSubShapeID2, JPH::RVec3Arg inContactPosition,
								JPH::Vec3Arg inContactNormal, JPH::CharacterContactSettings& ioSettings) override;

		void OnCharacterContactAdded(const JPH::CharacterVirtual* inCharacter,
									 const JPH::CharacterVirtual* inOtherCharacter,
									 const JPH::SubShapeID& inSubShapeID2, JPH::RVec3Arg inContactPosition,
									 JPH::Vec3Arg inContactNormal, JPH::CharacterContactSettings& ioSettings) override;
		void OnCharacterContactPersisted(const JPH::CharacterVirtual* inCharacter,
										 const JPH::CharacterVirtual* inOtherCharacter,
										 const JPH::SubShapeID& inSubShapeID2, JPH::RVec3Arg inContactPosition,
										 JPH::Vec3Arg inContactNormal,
										 JPH::CharacterContactSettings& ioSettings) override;

		void Record(const JPH::CharacterVirtual* character, const JPH::CharacterVirtual* other, JPH::RVec3Arg position,
					JPH::Vec3Arg normal, bool is_new);
		static void BlockRagdollPush(const JPH::BodyID& body_id, JPH::CharacterContactSettings& settings);

		SizedArray<CharacterBump> Bumps;
	};

	JPH::ObjectLayer GetCollisionLayer() const;
	void RefreshContacts();

	ContactListener mContactListener;

public:
	JPH::Ref<JPH::CharacterVirtual> pPlayerVirt;
	JPH::RefConst<JPH::Shape> pPhysicsShape;

	CharacterConfig Config;
	eCharacterRagdollCollision RagdollCollision = eCharacterRagdollCollision::Powered;

	JPH::Vec3 mMovementVector = JPH::Vec3::sZero();

	float32 JumpSpeed = 0.0f;
	float32 CoyoteTime = scDefaultCoyoteTime;
	float32 mTimeSinceGrounded = 1.0e6f;

	// float32 HeadRecoveryYOffset = 0.0f;
	float mTime = 0.01f;

	bool bIsGrounded : 1 = false;
	bool bCollisionEnabled : 1 = true;

	bool bDisableGravity : 1 = false;

	bool mbJumpPending : 1 = false;
	bool mbJumpConsumed : 1 = false;
};

} // namespace fx::physics
