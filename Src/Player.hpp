#pragma once

#include <Core/Ref.hpp>
#include <Math/MathUtil.hpp>
#include <Math/Vec2.hpp>
#include <Physics/PhysicsPlayer.hpp>
#include <Renderer/Camera.hpp>

namespace fx {

class Object;

class Player
{
	const Vec3f scMaxWalkSpeed = Vec3f(4.5f);
	const Vec3f scMaxSprintSpeed = Vec3f(5.5f);

	static constexpr float32 scMovementLerpSpeed = 10.0f;

	// TODO: Break these out into config vars
	static constexpr float32 scSprintFov = 85.0f;
	static constexpr float32 scWalkingFov = 80.0f;

	/// Fraction of each frame's camera turn that the view model trails behind by. Higher values sway further.
	static constexpr float32 scViewModelSwayAmount = 0.12f;
	/// How quickly (1/s) the view model settles back in line with the camera.
	static constexpr float32 scViewModelSwayReturnSpeed = 12.0f;
	/// The maximum angle (in radians) the view model can trail the camera by, so it stays on screen during flicks.
	static constexpr float32 scViewModelMaxSway = MathUtil::DegreesToRadians(5.0f);
	/// Roll per radian of yaw sway, so the view model banks into horizontal turns.
	static constexpr float32 scViewModelSwayRoll = 1.25f;

	static constexpr float32 scViewImpulseReturnSpeed = 20.0f;
	static constexpr float32 scViewImpulseActSpeed = 25.0f;

public:
	Player() = default;

	void Create();

	void Update(float64 delta_time);
	void MoveBy(const Vec3f& by);

	void DoFireAnimation();

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
	bool IsFlyMode() const { return Physics.bDisableGravity; };

	void Move(float64 delta_time, const Vec3f& offset);

	void RotateHead(const Vec2f& xy);

	FX_FORCE_INLINE Vec3f GetBob() const { return Vec3f(mHeadBobX, mHeadBobY, 0.0f); }

	~Player();

private:
	FX_FORCE_INLINE void RequireDirectionUpdate() { mbUpdateDirection = true; }
	FX_FORCE_INLINE void RequirePhysicsUpdate() { mbUpdatePhysicsTransform = true; }

	FX_FORCE_INLINE void MarkApplyingUserForce() { mbIsApplyingUserForce = true; }

	void UpdateViewModel(double delta_time);
	void UpdateViewModelSway(float32 delta_time);

	FX_FORCE_INLINE void UpdateDirection()
	{
		if (!mbUpdateDirection) {
			return;
		}

		float32 s_anglex, c_anglex;
		MathUtil::SinCos(pCamera->mAngleX, &s_anglex, &c_anglex);

		MovementDirection.Set(s_anglex, 0.0f, c_anglex);
		if (mbIsFlymode) {
			MovementDirection.Y = sin(pCamera->mAngleY);
		}

		// MovementDirection = Vec3f(c_angley, s_angley, c_angley) * MovementDirection;
		// Clear the movement direction's Y. This is because sin(mAngleY) = 0 when mAngleY is zero.
		// MovementDirection.Y = 0.0f;

		// CameraDirection.NormalizeIP();

		mbUpdateDirection = false;
	}

	FX_FORCE_INLINE void SyncPhysicsToPlayer() { Position.FromJoltVec3(Physics.pPlayerVirt->GetPosition()); }

public:
	physics::PhysicsPlayer Physics;
	Ref<PerspectiveCamera> pCamera { nullptr };

	/**
	 * @brief The direction that the player is facing. This is the direction the player moves in and disregards the
	 * pitch of the camera.
	 */
	Vec3f MovementDirection = Vec3f::sForward;
	Vec3f Position = Vec3f::sZero;

	float JumpForce = 0.0f;

	bool bIsSprinting : 1 = false;

	Vec2f HeadBobStrength = Vec2f { 0.011, 0.018 };

	float32 SpeedMultiplier = 1.0f;


	float32 mBobCounterY = 0.0f;

private:
	float32 mHeadBobX = 0.0f;
	float32 mHeadBobY = 0.0f;

	Vec3f mCameraOffset = Vec3f::sZero;

	Vec3f mMovementGoal = Vec3f::sZero;
	Vec3f mCameraGoal = Vec3f::sZero;

	Vec3f mUserForce = Vec3f::sZero;

	Quat mViewModelAccumRot = Quat::scIdentity;
	Quat mViewModelImpulseGoal = Quat::scIdentity;

	bool mbIsFiring = false;


	Object* mpViewModel = nullptr;

	/**
	 * @brief The view model's rotation: the camera's rotation with the sway applied in the camera's local space.
	 */
	Quat mViewModelRotation = Quat::scIdentity;
	/// How far (radians) the view model currently trails the camera's yaw and pitch.
	float32 mViewModelSwayYaw = 0.0f;
	float32 mViewModelSwayPitch = 0.0f;
	/// Camera angles from the previous view model update, used to find how far the camera turned.
	float32 mPrevCameraYaw = 0.0f;
	float32 mPrevCameraPitch = 0.0f;

	bool bBobReverse = false;
	bool mbIsApplyingUserForce : 1 = false;
	bool mbUpdateDirection : 1 = true;
	bool mbUpdatePhysicsTransform : 1 = true;

	bool mbIsFlymode : 1 = false;
};

} // namespace fx
