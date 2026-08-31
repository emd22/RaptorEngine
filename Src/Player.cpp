#include "Player.hpp"

#include <Core/RefUtil.hpp>
#include <Engine.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx {

using namespace renderer;

void Player::Create()
{
	pCamera = MakeRef<PerspectiveCamera>();

	pCamera->SetAspectRatio(gGraphics->Swapchain.GetAspectRatio());
	pCamera->SetFov(scWalkingFov);

	Physics.Create();

	// Since the physics position is the center of the capsule, we will use standing height / 2.
	mCameraOffset = Vec3f(0, physics::PhysicsPlayer::scStandingHeight, 0);
}

void Player::MoveBy(const Vec3f& by)
{
	Position += by;
	RequirePhysicsUpdate();
}

void Player::Jump()
{
	if (Physics.bIsGrounded && !mbIsFlymode) {
		JumpForce = 2.5f;
	}
}

void Player::SetFlyMode(bool value)
{
	mbIsFlymode = value;
	Physics.bDisableGravity = value;
}

void Player::Move(float64 delta_time, const Vec3f& offset)
{
	const Vec3f forward = MovementDirection * offset.Z;
	const Vec3f right = MovementDirection.Cross(Vec3f::sUp) * -offset.X;
	const Vec3f up = Vec3f::sUp * offset.Y;


	Vec3f movement_goal = (forward + right + up);

	if (movement_goal.Length() > 1e-3) {
		movement_goal.NormalizeIP();
	}

	mUserForce.SmoothInterpolate(movement_goal * (bIsSprinting ? scMaxSprintSpeed : scMaxWalkSpeed),
								 scMovementLerpSpeed, delta_time);

	if (movement_goal.Length() <= 0.25) {
		mBobCounterY = MathUtil::SmoothInterpolate(mBobCounterY, 0.0f, 10.0f, delta_time);
	}

	Vec3f force = mUserForce;

	if (!mbIsFlymode) {
		force.Y = JumpForce;
		JumpForce = 0;
	}

	Physics.ApplyMovement(force);
}

void Player::Update(float64 delta_time)
{
	Physics.Update(delta_time);
	SyncPhysicsToPlayer();

	UpdateDirection();
	pCamera->MoveTo(Position + mCameraOffset);

	const bool user_force_released = mUserForce.IsNearZero(0.1);

	// const bool should_reset_center = ((user_force_released || Physics.bIsGrounded) &&
	// 								  (MathUtil::IsCloseTo(mBobCounterY, 0.0f) == false));


	if (bEnableHeadBob && (Physics.bIsGrounded)) {
		float32 body_speed = mUserForce.Length();
		float32 counter_speed = (bBobReverse ? -1.9f : 1.9f);

		mBobCounterY += delta_time * counter_speed * body_speed;


		if (mBobCounterY > (FX_PI_2)) {
			bBobReverse = true;
		}
		else if (mBobCounterY < -(FX_PI_2)) {
			bBobReverse = false;
		}
	}


	mHeadBobX = HeadBobStrength.X * cosf(mBobCounterY + FX_PI_2);
	mHeadBobY = HeadBobStrength.Y * sinf(mBobCounterY + FX_PI_2);

	Vec3f bob_vector = pCamera->GetUpVector() * mHeadBobY + pCamera->GetRightVector() * mHeadBobX;
	pCamera->MoveBy(bob_vector);

	if (user_force_released == false) {
		if (bIsSprinting && pCamera->GetFov() < scSprintFov) {
			pCamera->SetFov(MathUtil::SmoothInterpolate(pCamera->GetFov(), scSprintFov, 8.0f, delta_time));
		}
		else if (!bIsSprinting && pCamera->GetFov() > scWalkingFov) {
			pCamera->SetFov(MathUtil::SmoothInterpolate(pCamera->GetFov(), scWalkingFov, 13.0f, delta_time));
		}
	}

	pCamera->Update();

	mbUpdatePhysicsTransform = false;
}

Player::~Player() {}

} // namespace fx
