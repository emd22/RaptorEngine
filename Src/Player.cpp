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
			mViewModelRotation = Quat::FromEulerAngles(pCamera->GetRotation());
			mPrevCameraRotation = mViewModelRotation;
			mViewModelAngularVelocity = Vec3f::sZero;
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

/**
 * @brief Convert a rotation to a rotation vector (axis * angle in radians), taking the shortest path.
 */
static Vec3f ToRotationVector(Quat quat)
{
	// q and -q are the same rotation, use the one with the smaller angle
	if (quat.W < 0.0f) {
		quat = Quat(-quat.X, -quat.Y, -quat.Z, -quat.W);
	}

	const Vec3f axis(quat.X, quat.Y, quat.Z);
	const float32 sin_half_angle = axis.Length();

	// For tiny angles, sin(angle / 2) ~= angle / 2
	if (sin_half_angle < 1e-7f) {
		return axis * 2.0f;
	}

	return axis * (2.0f * atan2f(sin_half_angle, quat.W) / sin_half_angle);
}

static Quat FromRotationVector(const Vec3f& rotation)
{
	const float32 angle = rotation.Length();

	if (angle < 1e-7f) {
		return Quat(rotation.X * 0.5f, rotation.Y * 0.5f, rotation.Z * 0.5f, 1.0f).Normalize();
	}

	return Quat::FromAxisAngle(rotation, angle);
}

void Player::UpdateViewModelLag(float32 delta_time)
{
	// The view model is attached to the camera by a damped spring. When the camera starts turning the view model trails
	// behind, the spring pulls it back in line while the camera is still turning, and when the camera stops the view
	// model's momentum carries it slightly past before settling.

	if (delta_time <= 0.0f) {
		return;
	}

	// const Quat camera_rotation = Quat::FromEulerAngles(mCameraGoal);
	// const Vec3f camera_angular_velocity = ToRotationVector(camera_rotation * mPrevCameraRotation.Conjugate()) /
	// 									  delta_time;

	// const float32 stiffness = scViewModelSpringFrequency * scViewModelSpringFrequency;
	// const float32 damping = 2.0f * scViewModelSpringDamping * scViewModelSpringFrequency;

	// Integrate in fixed size substeps (sweeping the camera across the frame) so the feel is the same at any framerate.
	// constexpr float32 cMaxStep = 1.0f / 240.0f;

	// const int32 num_steps = std::max(1, static_cast<int32>(ceilf(delta_time / cMaxStep)));
	// const float32 step = delta_time / static_cast<float32>(num_steps);

	// for (int32 i = 1; i <= num_steps; i++) {
	// 	const Quat step_camera_rotation = mPrevCameraRotation.SLerp(camera_rotation, static_cast<float32>(i) /
	// 																				   static_cast<float32>(num_steps));

	// 	const Vec3f lag = ToRotationVector(step_camera_rotation * mViewModelRotation.Conjugate());

	// 	// Implicit euler step of `accel = stiffness * lag + damping * (camera velocity - view model velocity)`, which
	// 	// stays stable for any step size.
	// 	mViewModelAngularVelocity = (mViewModelAngularVelocity +
	// 								 (lag * stiffness + camera_angular_velocity * damping) * step) /
	// 								(1.0f + damping * step + stiffness * step * step);

	// 	mViewModelRotation = (FromRotationVector(mViewModelAngularVelocity * step) * mViewModelRotation).Normalize();
	// }

	// mPrevCameraRotation = camera_rotation;

	// Hard limit on how far the view model can drift from the camera, so it stays on screen during fast flicks.
	// const Vec3f lag = ToRotationVector(camera_rotation * mViewModelRotation.Conjugate());
	// const float32 lag_angle = lag.Length();

	// if (lag_angle > scViewModelMaxLag) {
	// 	const Vec3f lag_direction = lag / lag_angle;
	// 	mViewModelRotation = FromRotationVector(lag_direction * -scViewModelMaxLag) * camera_rotation;

	// 	// Drop any velocity that would push the view model further past the limit, so it's dragged along with the
	// 	// camera.
	// 	const float32 outward_speed = (camera_angular_velocity - mViewModelAngularVelocity).Dot(lag_direction);
	// 	if (outward_speed > 0.0f) {
	// 		mViewModelAngularVelocity += lag_direction * outward_speed;
	// 	}
	// }
}

void Player::UpdateViewModel(double delta_time)
{
	if (mpViewModel == nullptr) {
		return;
	}


	mViewModelRotation = Quat::FromEulerAngles(pCamera->GetRotation());
	// UpdateViewModelLag(static_cast<float32>(delta_time));

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
