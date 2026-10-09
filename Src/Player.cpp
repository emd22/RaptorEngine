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

	mCameraOffset = Vec3f(0, physics::PhysicsPlayer::scEyeHeight, 0);

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

void Player::ResetMotion()
{
	mUserForce = Vec3f::sZero;
	mMovementGoal = Vec3f::sZero;
	mHorizontalSpeed = 0.0f;

	mLandingOffset = 0.0f;
	mLandingVelocity = 0.0f;
	mViewModelVerticalLag = 0.0f;
	mbWasOnGround = false;
}

static void CancelRecoilOffset(float32& offset, float32 input)
{
	if (offset * input < 0.0f) {
		offset += std::copysign(std::min(std::abs(input), std::abs(offset)), input);
	}
}

void Player::RotateCamera(const Vec2f& xy)
{
	pCamera->Rotate(xy.X, xy.Y);
	mCameraGoal = pCamera->GetRotation();

	RequireDirectionUpdate();
}

void Player::RotateHead(const Vec2f& xy)
{
	CancelRecoilOffset(mRecoilOffsetYaw, xy.X);
	CancelRecoilOffset(mRecoilOffsetPitch, xy.Y);

	RotateCamera(xy);
}

void Player::Jump()
{
	if (!mbIsFlymode) {
		Physics.Jump();
	}
}

void Player::SpawnAt(const Vec3f position, const Vec3f direction)
{
	TeleportTo(position);

	const Vec3f unit = direction.Normalize();
	const float32 yaw = std::atan2(unit.X, unit.Z);
	const float32 pitch = std::asin(std::clamp(unit.Y, -1.0f, 1.0f));

	pCamera->Rotate(std::remainder(yaw - pCamera->mAngleX, static_cast<float32>(FX_2PI)), pitch - pCamera->mAngleY);

	mCameraGoal = pCamera->GetRotation();
	mPrevCameraYaw = pCamera->mAngleX;
	mPrevCameraPitch = pCamera->mAngleY;
	mViewModelSwayYaw = 0.0f;
	mViewModelSwayPitch = 0.0f;

	RequireDirectionUpdate();
}

void Player::SetFlyMode(bool value)
{
	mbIsFlymode = value;
	Physics.bDisableGravity = value;

	mLandingOffset = 0.0f;
	mLandingVelocity = 0.0f;

	RequireDirectionUpdate();
}

void Player::Move(float64 delta_time, const Vec3f offset)
{
	const Vec3f forward = MovementDirection * offset.Z;
	const Vec3f right = mStrafeDirection * offset.X;
	const Vec3f up = mbIsFlymode ? Vec3f::sUp * offset.Y : Vec3f::sZero;

	mMovementGoal = (forward + right + up);

	if (mMovementGoal.Length() > 1e-3) {
		mMovementGoal.NormalizeIP();
	}

	const float32 max_speed = (bIsSprinting ? scSprintSpeed : scWalkSpeed) * SpeedMultiplier;

	mUserForce.SmoothInterpolate(mMovementGoal * max_speed, scMovementLerpSpeed, delta_time);

	if (mMovementGoal.Length() <= 0.25) {
		mBobCounterY = MathUtil::SmoothInterpolate(mBobCounterY, 0.0f, 10.0f, delta_time);
	}

	Vec3f force = mUserForce;

	if (!mbIsFlymode) {
		force.Y = 0.0f;
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
	const JPH::Vec3 character_velocity = Physics.pPlayerVirt->GetLinearVelocity();

	mViewModelVerticalLag = MathUtil::SmoothInterpolate(mViewModelVerticalLag,
														-character_velocity.GetY() * scViewModelVelocityLag,
														scViewModelVerticalLagSpeed, delta_time);

	view_model_bob += Vec3f(-character_velocity.GetX() * scViewModelVelocityLag,
							mViewModelVerticalLag - mLandingOffset * scLandingViewModelCounter,
							-character_velocity.GetZ() * scViewModelVelocityLag);

	// const float32 rotx = sin(gWorld->Player.mBobCounterY * 0.5f) * 0.05f;

	mpViewModel->SetPosition(pCamera->Position - (forward * (0.05 + mViewKickValue[ViewKickBack])) +
							 (up * (0.15 - mViewModelHolster * scHolsterDrop)) + (right * 0.0) + view_model_bob);
	const Quat kick = Quat::FromEulerAngles(
		Vec3f(-mViewKickValue[ViewKickPitch], mViewKickValue[ViewKickYaw], mViewKickValue[ViewKickRoll]));

	mpViewModel->SetRotation(mViewModelRotation * kick);
}

static float32 RandomUnitClosed()
{
	return static_cast<float32>(FastRand32() >> 8) * (2.0f / 16777215.0f) - 1.0f; // [-1.0, 1.0]
}

static float32 SpringImpulsePerPeak(float32 omega, float32 zeta)
{
	const float32 damped = omega * std::sqrt(1.0f - zeta * zeta);
	const float32 peak_time = std::atan2(damped, zeta * omega) / damped;
	const float32 peak_per_impulse = std::exp(-zeta * omega * peak_time) * std::sin(damped * peak_time) / damped;

	return 1.0f / peak_per_impulse;
}

float32 Player::ViewKickImpulsePerPeak() { return SpringImpulsePerPeak(scViewKickFrequency, scViewKickDamping); }

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

void Player::UpdateLanding(float32 delta_time)
{
	const JPH::CharacterVirtual& character = *Physics.pPlayerVirt;

	const bool on_ground = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

	if (on_ground && !mbWasOnGround && !mbIsFlymode) {
		const float32 impact_speed = character.GetGroundVelocity().GetY() - character.GetLinearVelocity().GetY();
		const float32 dip = std::min((impact_speed - scLandingMinImpactSpeed) * scLandingDipPerSpeed, scLandingMaxDip);

		if (dip > 0.0f) {
			mLandingVelocity -= dip * SpringImpulsePerPeak(scLandingFrequency, scLandingDamping);
		}
	}

	mbWasOnGround = on_ground;

	const float32 omega = scLandingFrequency;
	const float32 zeta = scLandingDamping;
	const float32 damped = omega * std::sqrt(1.0f - zeta * zeta);

	const float32 decay = std::exp(-zeta * omega * delta_time);
	const float32 cos_step = std::cos(damped * delta_time);
	const float32 sin_step = std::sin(damped * delta_time);

	const float32 x = mLandingOffset;
	const float32 v = mLandingVelocity;

	mLandingOffset = decay * (x * cos_step + ((v + zeta * omega * x) / damped) * sin_step);
	mLandingVelocity = decay * (v * cos_step - ((omega * omega * x + zeta * omega * v) / damped) * sin_step);

	mLandingOffset = std::clamp(mLandingOffset, -scLandingMaxDip * 1.5f, scLandingMaxDip * 1.5f);

	if (std::abs(mLandingOffset) < 1.0e-4f && std::abs(mLandingVelocity) < 1.0e-3f) {
		mLandingOffset = 0.0f;
		mLandingVelocity = 0.0f;
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

static constexpr float32 scRecoilSettleAngle = MathUtil::DegreesToRadians(0.01f);
static constexpr float32 scRecoilClampEpsilon = 1.0e-6f;

static float32 RecoverRecoilAxis(float32& offset, float32 fraction)
{
	float32 back = offset * fraction;

	if (std::abs(offset - back) < scRecoilSettleAngle) {
		back = offset;
	}

	offset -= back;

	return back;
}

void Player::UpdateRecoil(float32 delta_time)
{
	mRecoilIdleTime += delta_time;

	const float32 blend = 1.0f - std::exp(-delta_time * scRecoilApplySpeed);
	const float32 step_pitch = mRecoilPendingPitch * blend;
	const float32 step_yaw = mRecoilPendingYaw * blend;

	mRecoilPendingPitch -= step_pitch;
	mRecoilPendingYaw -= step_yaw;

	mRecoilOffsetPitch += step_pitch;
	mRecoilOffsetYaw += step_yaw;

	float32 delta_pitch = step_pitch;
	float32 delta_yaw = step_yaw;

	const float32 recovering = mRecoilIdleTime - scRecoilRecoveryDelay;

	if (recovering > 0.0f && mRecoilRecovery > 0.0f) {
		const float32 t = std::min(recovering / scRecoilRecoveryRamp, 1.0f);
		const float32 ease = t * t * (3.0f - 2.0f * t);
		const float32 fraction = 1.0f - std::exp(-delta_time * mRecoilRecovery * ease);

		delta_pitch -= RecoverRecoilAxis(mRecoilOffsetPitch, fraction);
		delta_yaw -= RecoverRecoilAxis(mRecoilOffsetYaw, fraction);
	}

	if (delta_pitch != 0.0f || delta_yaw != 0.0f) {
		const float32 pitch_before = pCamera->mAngleY;

		RotateCamera(Vec2f(delta_yaw, delta_pitch));

		const float32 clamped_pitch = delta_pitch - (pCamera->mAngleY - pitch_before);

		if (std::abs(clamped_pitch) > scRecoilClampEpsilon) {
			mRecoilOffsetPitch -= clamped_pitch;
		}
	}
}


void Player::UpdateMeasuredMotion(const Vec3f& previous_position, float32 delta_time)
{
	if (delta_time <= 1.0e-5f) {
		return;
	}

	const JPH::Vec3 ground_velocity = Physics.pPlayerVirt->GetGroundVelocity();
	const Vec3f moved = Position - previous_position;

	const float32 velocity_x = moved.X / delta_time - ground_velocity.GetX();
	const float32 velocity_z = moved.Z / delta_time - ground_velocity.GetZ();

	mHorizontalSpeed = std::sqrt(velocity_x * velocity_x + velocity_z * velocity_z);
}

void Player::UpdateFov(float32 delta_time, bool user_force_released)
{
	const bool sprint_fov = bIsSprinting && !user_force_released;

	const float32 target_fov = sprint_fov ? scSprintFov : scWalkingFov;
	const float32 fov_speed = sprint_fov ? 8.0f : 13.0f;
	const float32 current_fov = pCamera->GetFov();

	if (std::abs(current_fov - target_fov) <= 0.001f) {
		return;
	}

	float32 next_fov = MathUtil::SmoothInterpolate(current_fov, target_fov, fov_speed, delta_time);

	if (std::abs(next_fov - target_fov) < 0.01f) {
		next_fov = target_fov;
	}

	pCamera->SetFov(next_fov);
}

void Player::Update(float64 delta_time)
{
	const float64 physics_time = std::min(delta_time, static_cast<float64>(physics::PhysicsPlayer::scMaxStepTime));

	const Vec3f previous_position = Position;

	Physics.Update(physics_time);
	SyncPhysicsToPlayer();

	UpdateMeasuredMotion(previous_position, static_cast<float32>(physics_time));

	UpdateRecoil(static_cast<float32>(delta_time));

	UpdateLanding(static_cast<float32>(delta_time));

	UpdateDirection();
	pCamera->MoveTo(Position + mCameraOffset + Vec3f(0.0f, mLandingOffset, 0.0f));

	const bool user_force_released = mUserForce.IsNearZero(0.1);

	// const bool should_reset_center = ((user_force_released || Physics.bIsGrounded) &&
	// 								  (MathUtil::IsCloseTo(mBobCounterY, 0.0f) == false));

	UpdateViewKick(static_cast<float32>(delta_time));

	if (gCVars->Get("b_headbob_enabled", false) && (Physics.bIsGrounded)) {
		float32 body_speed = mHorizontalSpeed;
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

	UpdateFov(static_cast<float32>(delta_time), user_force_released);

	pCamera->Update();

	Weapons.Update(static_cast<float32>(delta_time));

	UpdateViewModel(delta_time);
}

Player::~Player() {}

} // namespace fx
