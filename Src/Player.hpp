#pragma once

#include <raptor_ffi.h>

#include <Asset/Animation.hpp>
#include <Core/Ref.hpp>
#include <Core/String.hpp>
#include <Math/MathUtil.hpp>
#include <Math/Vec2.hpp>
#include <Physics/PhysicsPlayer.hpp>
#include <Renderer/Camera.hpp>
#include <Weapon/WeaponSystem.hpp>

namespace fx {

class Object;

class Player
{
	// TODO: Break these out into config vars
	static constexpr float32 scWalkingFov = 80.0f;

	static constexpr const char* scViewModelIdleAnim = "IDLE";
	static constexpr const char* scViewModelFireAnim = "Armature|Fire";
	static constexpr const char* scViewModelReloadAnim = "Armature|ReloadClip";

	static constexpr float32 scDefaultViewKickDegrees = 2.5f;
	static constexpr float32 scDefaultViewKickback = 0.025f;

public:
	Player();

	Player(const Player&) = delete;
	Player& operator=(const Player&) = delete;

	void Create();

	void Update(float64 delta_time);
	void MoveBy(const Vec3f& by);

	void DoFireAnimation(float32 kick_degrees = scDefaultViewKickDegrees, float32 kickback = scDefaultViewKickback);
	void DoReloadAnimation();

	void SetViewModelAnimations(const char* idle, const char* fire, const char* reload);
	void SetViewModelHolster(float32 amount) { mpState->holster = amount; }

	void AddRecoil(float32 pitch, float32 yaw);
	void SetRecoilRecovery(float32 rate_per_second) { mpState->recoil.recovery = rate_per_second; }

	void Jump();

	/**
	 * @brief Move the player and its physics by `offset`.
	 */
	void TeleportBy(const Vec3f& offset)
	{
		SyncPhysicsToPlayer();

		Position += offset;
		Physics.Teleport(Position);
	}

	/**
	 * @brief Move the player and its physics by `offset`.
	 */
	void TeleportTo(const Vec3f& position)
	{
		Position = position;
		Physics.Teleport(Position);
	}

	void SetFlyMode(bool value);
	bool IsFlyMode() const { return Physics.IsGravityDisabled(); }

	void Move(float64 delta_time, const Vec3f& offset);

	void RotateHead(const Vec2f& xy);

	~Player() { rx_player_state_free(mpState); }

private:
	void UpdateViewModel(double delta_time);

	FX_FORCE_INLINE void SyncPhysicsToPlayer() { Position = Physics.GetPosition(); }

private:
	RxPlayerState* mpState = rx_player_state_new();

public:
	physics::PhysicsPlayer Physics;
	Ref<PerspectiveCamera> pCamera { nullptr };

	/**
	 * @brief The direction that the player is facing. This is the direction the player moves in and disregards the
	 * pitch of the camera.
	 */
	Vec3f& MovementDirection = *reinterpret_cast<Vec3f*>(mpState->movement_direction);
	Vec3f& Position = *reinterpret_cast<Vec3f*>(mpState->position);

	float32& JumpForce = mpState->jump_force;

	bool& bIsSprinting = *reinterpret_cast<bool*>(&mpState->sprinting);

	Vec2f& HeadBobStrength = *reinterpret_cast<Vec2f*>(mpState->head_bob_strength);

	float32& SpeedMultiplier = mpState->speed_multiplier;

	weapon::WeaponSystem Weapons;

private:
	enum eViewKickChannel : uint32
	{
		ViewKickPitch,
		ViewKickYaw,
		ViewKickRoll,
		ViewKickBack,
		ViewKickChannelCount,
	};

	static_assert(ViewKickChannelCount == 4);

	String mIdleAnim = scViewModelIdleAnim;
	String mFireAnim = scViewModelFireAnim;
	String mReloadAnim = scViewModelReloadAnim;

	Object* mpViewModel = nullptr;
	/// Shared by every mesh in the view model. Null if the view model has no skeleton.
	Ref<Skeleton> mpViewModelSkeleton { nullptr };
};

} // namespace fx
