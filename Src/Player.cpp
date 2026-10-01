#include "Player.hpp"

#include <Asset/AssetManager.hpp>
#include <CVar.hpp>
#include <Core/Random.hpp>
#include <Core/RefUtil.hpp>
#include <Engine.hpp>
#include <Object/ObjectManager.hpp>
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

	AssetTicket view_model = gAssetManager->LoadObject("view_model",
													   "RaptorData/Data/Demo/Models/viewmodel/viewmodel_baked.glb");

	// Registered before World::Attach() so these callbacks run first: the view model has to be set up before the world
	// adds it to the grid, or a probe bake running on the main thread in between would capture it as level geometry.
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
			object->SetProbeVisible(false);

			if (object->pSkeleton.IsValid()) {
			}

			mpViewModel = object;

			// The skeleton lives on whichever mesh is skinned, which is one of the attached objects if the model has
			// more than one mesh.
			mpViewModelSkeleton = object->pSkeleton;

			for (ObjectID child_id : object->AttachedNodes) {
				if (mpViewModelSkeleton) {
					break;
				}

				Object* child = gObjectManager->GetObject(child_id);
				if (child != nullptr) {
					mpViewModelSkeleton = child->pSkeleton;
				}
			}

			if (mpViewModelSkeleton) {
				mpViewModelSkeleton->SetRestAnimation(mpViewModelSkeleton->FindAnimation(mIdleAnim));
			}

			// Start in line with the camera so the view model doesn't swing in when it first appears
			mPrevCameraYaw = pCamera->mAngleX;
			mPrevCameraPitch = pCamera->mAngleY;
			mViewModelSwayYaw = 0.0f;
			mViewModelSwayPitch = 0.0f;
		});

	gWorld->Attach(view_model);

	Weapons.Create(this);
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
	const Quat sway = Quat::FromEulerAngles(Vec3f(-mViewModelSwayPitch + mViewModelHolster * scHolsterPitch,
												  mViewModelSwayYaw, mViewModelSwayYaw * scViewModelSwayRoll));

	mViewModelRotation = Quat::FromEulerAngles(pCamera->GetRotation()) * sway;

	// Build the basis from the lagging rotation (rather than the camera's) so the view model pivots around the eye.
	const Mat4f view_model_basis = Mat4f::AsRotation(mViewModelRotation);

	const Vec3f right = Vec3f(view_model_basis.Rows[0]);
	const Vec3f up = Vec3f(view_model_basis.Rows[1]);
	const Vec3f forward = Vec3f(view_model_basis.Rows[2]);

	float32 horizontal_scale = 0.19f;
	float32 vertical_scale = 0.15f;

	// if (bIsSprinting) {
	// 	horizontal_scale = 0.30f;
	// 	vertical_scale = 0.3f;
	// }

	// Negate the bob (l'eponge) to reduce motion and make the movement feel more cohesive.
	const Vec3f bob = -GetBob();

	Vec3f view_model_bob = (right * (bob.X * horizontal_scale)) + (up * (bob.Y * vertical_scale));
	view_model_bob += Vec3f(-Physics.pPlayerVirt->GetLinearVelocity() * 0.004f);

	// const float32 rotx = sin(gWorld->Player.mBobCounterY * 0.5f) * 0.05f;

	mpViewModel->SetPosition(pCamera->Position - (forward * (0.05 + mViewKickValue[ViewKickBack])) + (up * (0.15 - mViewModelHolster * scHolsterDrop)) +
							 (right * 0.0) + view_model_bob);
	const Quat kick = Quat::FromEulerAngles(
		Vec3f(-mViewKickValue[ViewKickPitch], mViewKickValue[ViewKickYaw], mViewKickValue[ViewKickRoll]));

	mpViewModel->SetRotation(mViewModelRotation * kick);
}

static float32 RandomUnitClosed()
{
	return static_cast<float32>(FastRand32() >> 8) * (2.0f / 16777215.0f) - 1.0f; // [-1.0, 1.0]
}

float32 Player::ViewKickImpulsePerPeak()
{
	const float32 omega = scViewKickFrequency;
	const float32 zeta = scViewKickDamping;
	const float32 damped = omega * std::sqrt(1.0f - zeta * zeta);
	const float32 peak_time = std::atan2(damped, zeta * omega) / damped;
	const float32 peak_per_impulse = std::exp(-zeta * omega * peak_time) * std::sin(damped * peak_time) / damped;

	return 1.0f / peak_per_impulse;
}

void Player::UpdateViewKick(float32 delta_time)
{
	const float32 omega = scViewKickFrequency;
	const float32 zeta = scViewKickDamping;
	const float32 damped = omega * std::sqrt(1.0f - zeta * zeta);

	const float32 decay = std::exp(-zeta * omega * delta_time);
	const float32 cos_step = std::cos(damped * delta_time);
	const float32 sin_step = std::sin(damped * delta_time);

	for (uint32 i = 0; i < ViewKickChannelCount; i++) {
		const float32 x = mViewKickValue[i];
		const float32 v = mViewKickVelocity[i];

		const float32 next_x = decay * (x * cos_step + ((v + zeta * omega * x) / damped) * sin_step);
		const float32 next_v = decay * (v * cos_step - ((omega * omega * x + zeta * omega * v) / damped) * sin_step);

		mViewKickValue[i] = std::clamp(next_x, -mViewKickBound[i], mViewKickBound[i]);
		mViewKickVelocity[i] = next_v;
	}
}

void Player::DoFireAnimation(float32 kick_degrees, float32 kickback)
{
	const float32 pitch = MathUtil::DegreesToRadians(kick_degrees);

	const float32 peaks[ViewKickChannelCount] = {
		pitch,
		pitch * scViewKickYawRatio * RandomUnitClosed(),
		pitch * scViewKickRollRatio * RandomUnitClosed(),
		kickback,
	};

	const float32 bounds[ViewKickChannelCount] = {
		pitch,
		pitch * scViewKickYawRatio,
		pitch * scViewKickRollRatio,
		kickback,
	};

	const float32 impulse_per_peak = ViewKickImpulsePerPeak();

	for (uint32 i = 0; i < ViewKickChannelCount; i++) {
		mViewKickVelocity[i] += peaks[i] * impulse_per_peak;
		mViewKickBound[i] = std::max(mViewKickBound[i], bounds[i] * scViewKickLimit);
	}

	mbIsFiring = true;

	if (!mpViewModelSkeleton) {
		return;
	}

	const Animation* fire = mpViewModelSkeleton->FindAnimation(mFireAnim);

	if (fire == nullptr) {
		return;
	}

	// Firing again mid-shot restarts the shot rather than stacking another one on top of it
	if (mpViewModelSkeleton->GetActiveAnimation() == fire) {
		mpViewModelSkeleton->PopAnimation();
	}

	mpViewModelSkeleton->PushAnimation(fire);
}

void Player::DoReloadAnimation()
{
	if (!mpViewModelSkeleton) {
		return;
	}

	const Animation* reload = mpViewModelSkeleton->FindAnimation(mReloadAnim);

	if (reload == nullptr || mpViewModelSkeleton->GetActiveAnimation() == reload) {
		return;
	}

	mpViewModelSkeleton->PushAnimation(reload);
}

void Player::SetViewModelAnimations(const char* idle, const char* fire, const char* reload)
{
	mIdleAnim = idle;
	mFireAnim = fire;
	mReloadAnim = reload;

	if (!mpViewModelSkeleton) {
		return;
	}

	const Animation* idle_anim = mpViewModelSkeleton->FindAnimation(mIdleAnim);

	if (idle_anim != nullptr) {
		mpViewModelSkeleton->SetRestAnimation(idle_anim);
	}
}

void Player::AddRecoil(float32 pitch, float32 yaw)
{
	mRecoilPendingPitch += pitch;
	mRecoilPendingYaw += yaw;
	mRecoilIdleTime = 0.0f;
}

void Player::UpdateRecoil(float32 delta_time)
{
	mRecoilIdleTime += delta_time;

	const float32 blend = 1.0f - std::exp(-delta_time * scRecoilApplySpeed);
	const float32 step_pitch = mRecoilPendingPitch * blend;
	const float32 step_yaw = mRecoilPendingYaw * blend;

	mRecoilPendingPitch -= step_pitch;
	mRecoilPendingYaw -= step_yaw;

	float32 delta_pitch = step_pitch;
	float32 delta_yaw = step_yaw;

	mRecoilAppliedPitch += step_pitch;
	mRecoilAppliedYaw += step_yaw;

	if (mRecoilIdleTime > scRecoilRecoveryDelay && mRecoilRecovery > 0.0f) {
		const float32 applied = std::sqrt(mRecoilAppliedPitch * mRecoilAppliedPitch +
										  mRecoilAppliedYaw * mRecoilAppliedYaw);

		if (applied > 0.0f) {
			const float32 fraction = std::min(applied, mRecoilRecovery * delta_time) / applied;
			const float32 back_pitch = mRecoilAppliedPitch * fraction;
			const float32 back_yaw = mRecoilAppliedYaw * fraction;

			mRecoilAppliedPitch -= back_pitch;
			mRecoilAppliedYaw -= back_yaw;

			delta_pitch -= back_pitch;
			delta_yaw -= back_yaw;
		}
	}

	if (delta_pitch != 0.0f || delta_yaw != 0.0f) {
		RotateHead(Vec2f(delta_yaw, delta_pitch));
	}
}


void Player::Update(float64 delta_time)
{
	Physics.Update(delta_time);
	SyncPhysicsToPlayer();

	UpdateRecoil(static_cast<float32>(delta_time));

	UpdateDirection();
	pCamera->MoveTo(Position + mCameraOffset);

	const bool user_force_released = mUserForce.IsNearZero(0.1);

	// const bool should_reset_center = ((user_force_released || Physics.bIsGrounded) &&
	// 								  (MathUtil::IsCloseTo(mBobCounterY, 0.0f) == false));

	UpdateViewKick(static_cast<float32>(delta_time));

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


	float32 bob_sin, bob_cos;
	MathUtil::SinCos(mBobCounterY + FX_PI_2, &bob_sin, &bob_cos);

	mHeadBobX = HeadBobStrength.X * bob_cos;
	mHeadBobY = HeadBobStrength.Y * bob_sin;

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

	Weapons.Update(static_cast<float32>(delta_time));

	mbUpdatePhysicsTransform = false;

	UpdateViewModel(delta_time);
}

Player::~Player() {}

} // namespace fx
