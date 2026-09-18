#include "Player.hpp"

#include <Asset/AssetManager.hpp>
#include <CVar.hpp>
#include <Core/RefUtil.hpp>
#include <Engine.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <World.hpp>

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

	// Load the view model

	AssetTicket view_model = gAssetManager->LoadObject("view_model", "Data/Demo/Models/viewmodel.glb");
	gWorld->Attach(view_model);
	view_model.OnLoaded(
		[&](void* item_ptr)
		{
			Object* object = static_cast<Object*>(item_ptr);
			if (object == nullptr) {
				return;
			}

			object->SetShadowCaster(false);
			object->SetCullable(false);
			object->SetObjectLayer(eObjectLayer::PlayerLayer);

			mpViewModel = object;

			// Start in line with the camera so the view model doesn't swing in when it first appears
			mPrevCameraYaw = pCamera->mAngleX;
			mPrevCameraPitch = pCamera->mAngleY;
			mViewModelSwayYaw = 0.0f;
			mViewModelSwayPitch = 0.0f;
		});
}

void Player::MoveBy(const Vec3f& by)
{
	Position += by;
	RequirePhysicsUpdate();
}

void Player::RotateHead(const Vec2f& xy)
{
	pCamera->Rotate(xy.X, xy.Y);
	mCameraGoal = pCamera->GetRotation();

	RequireDirectionUpdate();
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

	mMovementGoal = (forward + right + up);

	if (mMovementGoal.Length() > 1e-3) {
		mMovementGoal.NormalizeIP();
	}

	mUserForce.SmoothInterpolate(mMovementGoal * (bIsSprinting ? scMaxSprintSpeed : scMaxWalkSpeed) *
									 Vec3f(SpeedMultiplier),
								 scMovementLerpSpeed, delta_time);

	if (mMovementGoal.Length() <= 0.25) {
		mBobCounterY = MathUtil::SmoothInterpolate(mBobCounterY, 0.0f, 10.0f, delta_time);
	}

	Vec3f force = mUserForce;

	if (!mbIsFlymode) {
		force.Y = JumpForce;
		JumpForce = 0;
	}

	Physics.ApplyMovement(force);
}

void Player::UpdateViewModelSway(float32 delta_time)
{
	// Yaw wraps at 2pi, so take the short way around to avoid a full-turn spike when it does.
	const float32 yaw_delta = std::remainder(pCamera->mAngleX - mPrevCameraYaw, static_cast<float32>(FX_2PI));
	const float32 pitch_delta = pCamera->mAngleY - mPrevCameraPitch;

	mPrevCameraYaw = pCamera->mAngleX;
	mPrevCameraPitch = pCamera->mAngleY;

	// Ease back in line with the camera, then trail behind by a fraction of however far the camera turned this frame.
	mViewModelSwayYaw = MathUtil::SmoothInterpolate(mViewModelSwayYaw, 0.0f, scViewModelSwayReturnSpeed, delta_time);
	mViewModelSwayPitch = MathUtil::SmoothInterpolate(mViewModelSwayPitch, 0.0f, scViewModelSwayReturnSpeed,
													  delta_time);

	mViewModelSwayYaw = MathUtil::Clamp(mViewModelSwayYaw - yaw_delta * scViewModelSwayAmount, -scViewModelMaxSway,
										scViewModelMaxSway);
	mViewModelSwayPitch = MathUtil::Clamp(mViewModelSwayPitch - pitch_delta * scViewModelSwayAmount,
										  -scViewModelMaxSway, scViewModelMaxSway);
}

void Player::UpdateViewModel(double delta_time)
{
	if (mpViewModel == nullptr) {
		return;
	}

	UpdateViewModelSway(static_cast<float32>(delta_time));

	// Pitch/yaw map to euler angles the same way as PerspectiveCamera::GetRotation(). Roll goes in this local offset
	// rather than the camera's euler angles, as FromEulerAngles applies Z last in world space.
	const Quat sway = Quat::FromEulerAngles(
		Vec3f(-mViewModelSwayPitch, mViewModelSwayYaw, mViewModelSwayYaw * scViewModelSwayRoll));

	mViewModelRotation = Quat::FromEulerAngles(pCamera->GetRotation()) * sway;

	// Build the basis from the lagging rotation (rather than the camera's) so the view model pivots around the eye.
	const Mat4f view_model_basis = Mat4f::AsRotation(mViewModelRotation);

	const Vec3f right = Vec3f(view_model_basis.Rows[0]);
	const Vec3f up = Vec3f(view_model_basis.Rows[1]);
	const Vec3f forward = Vec3f(view_model_basis.Rows[2]);

	float32 horizontal_scale = 0.25f;
	float32 vertical_scale = 0.2f;

	if (bIsSprinting) {
		horizontal_scale = 0.30f;
		vertical_scale = 0.3f;
	}

	// Negate the bob (l'eponge) to reduce motion and make the movement feel more cohesive.
	const Vec3f bob = -GetBob();

	Vec3f view_model_bob = (right * (bob.X * horizontal_scale)) + (up * (bob.Y * vertical_scale));
	view_model_bob += Vec3f(-Physics.pPlayerVirt->GetLinearVelocity() * 0.004f);

	// const float32 rotx = sin(gWorld->Player.mBobCounterY * 0.5f) * 0.05f;

	mpViewModel->SetPosition(pCamera->Position + (forward * 0.110) - (up * 0.265) + view_model_bob);

	mpViewModel->SetRotation(mViewModelRotation);
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


	if (gCVars->Get("b_headbob_enabled", false) && (Physics.bIsGrounded)) {
		float32 body_speed = mUserForce.Length();
		float32 counter_speed = (bBobReverse ? -2.3f : 2.3f);

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

	UpdateViewModel(delta_time);
}

Player::~Player() {}

} // namespace fx
