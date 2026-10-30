#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Character/CharacterVirtual.h>

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <Physics/PhysicsPlayer.hpp>

namespace fx {

/// User data on NPC capsules: a tag in the high 32 bits and the NPC's ragdoll serial in the low bits
namespace NPCCharacterUserData {
static constexpr uint64 scTag = 0x4E504343ull << 32;
static constexpr uint64 scTagMask = 0xFFFFFFFF00000000ull;

FX_FORCE_INLINE bool Matches(uint64 user_data) { return (user_data & scTagMask) == scTag; }
FX_FORCE_INLINE uint32 GetSerial(uint64 user_data) { return static_cast<uint32>(user_data & 0xFFFFFFFFull); }
} // namespace NPCCharacterUserData

/**
 * @brief The capsule an animated NPC walks with, built from the player's character settings. It is switched off while
 * the NPC is physical, so the ragdoll is the only thing that collides.
 */
class NPCMover
{
public:
	/// Grounded and still for this long before the capsule stops being updated
	static constexpr float32 scRestDelay = 0.25f;
	/// A resting capsule still refreshes this often, so it notices the floor going away
	static constexpr float32 scRestRefreshTime = 0.5f;

public:
	NPCMover() = default;
	NPCMover(const NPCMover&) = delete;
	NPCMover& operator=(const NPCMover&) = delete;

	bool Create(const physics::CharacterConfig& config, const Vec3f position, uint32 serial);
	void Destroy();

	void SetEnabled(bool enabled);
	void Teleport(const Vec3f position);

	/// Moves the capsule with `desired_velocity` along the ground, with gravity and stair stepping like the player
	void Update(float32 delta_time, const Vec3f desired_velocity);

	Vec3f GetPosition() const;
	Vec3f GetVelocity() const;
	bool IsGrounded() const;

	FX_FORCE_INLINE bool IsCreated() const { return mpCharacter != nullptr; }
	FX_FORCE_INLINE bool IsEnabled() const { return mbEnabled; }
	FX_FORCE_INLINE bool IsResting() const { return mRestTime >= scRestDelay; }
	FX_FORCE_INLINE const physics::CharacterConfig& GetConfig() const { return mConfig; }

	~NPCMover() { Destroy(); }

private:
	void RefreshContacts();

	JPH::Ref<JPH::CharacterVirtual> mpCharacter;
	physics::CharacterConfig mConfig;

	bool mbEnabled = false;
	float32 mRestTime = 0.0f;
	float32 mRefreshTime = 0.0f;
};

} // namespace fx
