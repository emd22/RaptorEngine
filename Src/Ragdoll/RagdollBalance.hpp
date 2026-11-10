#pragma once

#include "RagdollMath.hpp"
#include "RagdollRig.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyInterface.h>

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <algorithm>

namespace fx::physics {

class Ragdoll;

enum class eBalanceState : uint8
{
	Off,
	Standing,
	Stepping,
	Fallen,
};

enum class eFallReason : uint8
{
	None,
	Tilt,
	Height,
	Contact,
	/// The capture point has moved out of reach of any run of steps
	Unviable,
};

/**
 * @brief Limits of the centre of mass control: a horizontal force the loaded legs put through the ground as joint
 * torques, as far as the sole's size and friction allow
 */
struct BalanceAssist
{
	/// Largest force as a fraction of body weight, put through the legs
	float32 MaxForceFraction = 0.15f;
	/// The force fades out as the capture point moves this far outside the support (m). 0 never releases
	float32 ReleaseDistance = 0.1f;
	/// Once released, a braking-only force up to this fraction of body weight still slows the body while it steps
	float32 RecoveryForceFraction = 0.5f;
	/// Largest part of those forces that can skip the legs, as a fraction of body weight, for what the sole can't carry
	float32 ResidualFraction = 0.15f;
	/// External upward force at the centre of mass during a recoverable stumble, as a fraction of body weight
	float32 LiftFraction = 0.0f;
	/// Above zero, the residual force acts on the pelvis alone rather than as a pair with the foot
	float32 ExternalResidual = 0.0f;
	/// External damping of the pelvis against its own smoothed velocity (1/s, multiplied by body mass). 0 is off
	float32 PelvisDamping = 5.0f;
	/// How long a shove's stagger drift lasts (s): longer keeps a shoved body stumbling for more steps
	float32 StaggerDecayTime = 0.75f;
	/// How long each shove leaves the body unassisted (s)
	float32 StumbleTime = 0.3f;
	/// How long after a shove the body keeps still before its first catch step (s). The stagger lean eases in over it
	/// too
	float32 ReactionTime = 0.08f;
};

struct BalanceStatus
{
	eBalanceState State = eBalanceState::Off;
	eFallReason FallReason = eFallReason::None;
	uint32 Steps = 0;
	bool bCalm = false;
	/// Horizontal distance from the support to the capture point (m), zero while the capture point is over the feet
	JPH::Vec3 CaptureError = JPH::Vec3::sZero();
	/// Fraction of the stumble left after a shove
	float32 Stumble = 0.0f;

	/// Fraction of the reaction delay that has passed since the shove
	float32 Reaction = 0.0f;

	/// How far the body has got into a fall, 0 upright to 1 at the threshold where it counts as fallen
	float32 FallProgress = 0.0f;

	/// Size of the last force put straight on the pelvis (N), which skips the legs
	float32 AssistForce = 0.0f;

	/// Horizontal force the loaded legs are asked to put through the ground (N)
	float32 LegForce = 0.0f;

	/// Total time a leg meant to be standing has had its foot off the ground (s)
	float32 HangingTime = 0.0f;
	/// How many times a standing leg's foot has lost or regained the ground
	uint32 ContactFlips = 0;
	/// Time swinging feet have spent past their landing time, still short of the ground (s)
	float32 SwingOvertime = 0.0f;
	/// Swings given up on and started again because the foot never got to the ground
	uint32 SwingRetries = 0;
	/// Largest move of a swinging ankle's target in one step (m)
	float32 PeakSwingTargetStep = 0.0f;
	/// Total distance landings have moved while their foot was in the air (m)
	float32 SwingLandingTravel = 0.0f;
};

/// A rig leg plus what the balance controller senses and plans for it each substep
struct BalanceLeg : RagdollRigLeg
{
	JPH::Vec3 AnklePosition = JPH::Vec3::sZero();
	JPH::Quat FootOrientation = JPH::Quat::sIdentity();
	JPH::Vec3 FootCenter = JPH::Vec3::sZero();
	/// The sole's corners in world space, wherever the foot is
	JPH::Vec3 SoleWorld[scSoleCorners];
	JPH::Vec3 SoleUp = JPH::Vec3::sAxisY();

	/// Where the foot really touches. A foot rolling onto its edge has only that edge, and an airborne one nothing
	JPH::Vec3 ContactPoints[BodyGroundContact::scMaxPoints];
	uint32 ContactCount = 0;
	JPH::Vec3 ContactNormal = JPH::Vec3::sAxisY();
	float32 ContactFriction = 0.0f;
	/// Height of the surface this foot last touched
	float32 SurfaceHeight = 0.0f;
	float32 SoleHeight = 0.0f;
	float32 ContactAge = 0.0f;
	bool bTouching = false;
	bool bContact = false;

	bool bStance = true;
	bool bWasTouching = false;
	JPH::Quat DesiredThigh = JPH::Quat::sIdentity();
};

class RagdollBalance
{
private:
	static constexpr float32 scContactHoldTime = 0.08f;

	struct Swing
	{
		uint32 Leg = 0;
		float32 Time = 0.0f;
		float32 Duration = 0.22f;
		JPH::Vec3 StartAnkle = JPH::Vec3::sZero();
		JPH::Vec3 Landing = JPH::Vec3::sZero();
		JPH::Vec3 AnkleTarget = JPH::Vec3::sZero();
		/// Where the swing has got to on its way to the landing, before lift and detour
		JPH::Vec3 PathPoint = JPH::Vec3::sZero();
		/// Height of the path between surfaces of different heights, which it climbs first or descends last
		float32 PathHeight = 0.0f;
		/// Height of the surface the foot left
		float32 StartSurface = 0.0f;
		float32 Travel = 0.0f;
		bool bLifted = false;
		bool bFixed = false;
		bool bSwapped = false;
	};

public:
	bool Init(Ragdoll& ragdoll);
	void Reset();
	void Clear(Ragdoll& ragdoll);

	void Step(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, float32 delta_time, const JPH::Vec3 gravity,
			  bool contacts_fresh);

	void DebugDraw() const;

	FX_FORCE_INLINE bool IsValid() const { return mbValid; }
	FX_FORCE_INLINE void SetAssist(const BalanceAssist& assist) { mAssist = assist; }

	/// Velocity the body is let drift at after a shove instead of being held still, so it staggers a few steps. It
	/// decays on its own while balancing and is cleared when balance stops
	FX_FORCE_INLINE void SetStaggerVelocity(const JPH::Vec3 velocity)
	{
		// Stagger speed (m/s) above which a new shove finds the body already reacting
		constexpr float32 scReactionRestartSpeed = 0.25f;

		const bool is_drifting = mStaggerVelocity.LengthSq() > scReactionRestartSpeed * scReactionRestartSpeed;

		mStaggerVelocity = velocity;

		if (velocity.LengthSq() > 0.0f) {
			// A hit on a body already stumbling extends the stumble without restarting the reaction, so rapid hits
			// still get stepped into
			if (mStumbleTime <= 0.0f && !is_drifting) {
				mReactionElapsed = 0.0f;
			}

			mStumbleTime = mAssist.StumbleTime;
			mbStumbleStepped = false;
		}
	}
	FX_FORCE_INLINE JPH::Vec3 GetStaggerVelocity() const { return mStaggerVelocity; }
	/// Drops the stance height by this much (m), easing back out, as when a shot leg gives way for a moment
	FX_FORCE_INLINE void AddBuckle(float32 depth) { mLandingDip = std::max(mLandingDip, depth); }
	FX_FORCE_INLINE const BalanceStatus& GetStatus() const { return mStatus; }

private:
	bool InitRestPose(const Ragdoll& ragdoll);

	void GatherState(Ragdoll& ragdoll, float32 delta_time, const JPH::Vec3 gravity, bool contacts_fresh);
	void GatherFeet(Ragdoll& ragdoll, float32 delta_time, bool contacts_fresh);
	bool IsAnyBodyOnGround(Ragdoll& ragdoll) const;
	void BuildSupport();
	void BeginStumbleStep();

	bool DetectFall();
	void Fall(eFallReason reason);
	void UpdateStanding(float32 delta_time);
	void UpdateStepping(float32 delta_time);
	void UpdateClosingStep(float32 delta_time);
	bool NeedsClosingStep() const;
	bool BeginStep();
	void StartSwing(uint32 swing_leg, const JPH::Vec3 landing, bool fixed, float32 duration);
	JPH::Vec3 PlanStep(uint32 swing_leg, uint32 stance_leg, float32& out_time) const;
	JPH::Vec3 PredictCapture(uint32 stance_leg, float32 time) const;
	JPH::Vec3 GetSwingDetour(const JPH::Vec3 start, const JPH::Vec3 end, float32 travel) const;
	bool IsViable() const;

	float32 GetWeightShare(uint32 leg_index) const;
	float32 GetCaptureDistance(const BalanceLeg& leg) const;
	float32 GetLandingCaptureError(uint32 stance_leg, const JPH::Vec3 landing, float32 swing_time) const;
	JPH::Vec3 PlanLanding(uint32 swing_leg, uint32 stance_leg, float32 time_left) const;
	JPH::Vec3 FitLanding(uint32 swing_leg, uint32 stance_leg, JPH::Vec3 landing) const;
	JPH::Vec3 GetLandingAnkle(const BalanceLeg& leg, const JPH::Vec3 landing) const;

	void DriveStanceLegs(Ragdoll& ragdoll);
	void DriveSwingLeg(Ragdoll& ragdoll);
	void DriveHead(Ragdoll& ragdoll);
	JPH::Quat PitchFoot(const JPH::Quat& foot, float32 radians) const;
	void DriveFreeLeg(Ragdoll& ragdoll, const BalanceLeg& leg, const JPH::Vec3 ankle, const JPH::Quat& foot);
	void SolveLeg(Ragdoll& ragdoll, const BalanceLeg& leg, const JPH::Vec3 hip, const JPH::Vec3 ankle,
				  JPH::Quat& out_thigh, JPH::Quat& out_shin) const;
	void ApplyTorsoControl(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, const JPH::Vec3 gravity);
	void ApplyLegForces(Ragdoll& ragdoll, JPH::BodyInterface& body_interface);
	void ApplyExternalSupport(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, bool loaded);
	JPH::Vec3 GetCenterOfMassForce(uint32 loaded_count, const uint32* loaded, float32 mass,
								   float32 strength_scale) const;
	bool IsPressureInside(const BalanceLeg& leg, const JPH::Vec3 torque, float32 normal_force) const;
	JPH::Vec3 LimitAnkleTorque(const BalanceLeg& leg, const JPH::Vec3 torque, float32 normal_force) const;
	void ApplyJointTorque(Ragdoll& ragdoll, JPH::BodyInterface& body_interface, uint32 body, const JPH::Vec3 torque);
	void ApplyTrunkSpinDamping(Ragdoll& ragdoll, JPH::BodyInterface& body_interface);
	bool IsLoaded(const BalanceLeg& leg) const;

	JPH::Vec3 GetHeadingForward(const JPH::Quat& orientation, const JPH::Quat& rest) const;
	JPH::Quat GetHeadingRotation(const JPH::Vec3 forward) const;
	JPH::Vec3 ToGround(const JPH::Vec3 point) const;
	/// Where a foot would land at this point, on the highest surface under its sole
	JPH::Vec3 ToLanding(const JPH::Vec3 point) const;
	bool ProbeSurface(const JPH::Vec3 point, float32& out_height) const;
	float32 GetGroundAt(const JPH::Vec3 point) const;

private:
	bool mbValid = false;
	BalanceAssist mAssist;
	BalanceLeg mLegs[RagdollRig::scLimbCount];

	const RagdollRig* mpRig = nullptr;

	JPH::Quat mRestPelvis = JPH::Quat::sIdentity();

	float32 mRestComHeight = 1.0f;
	float32 mRestWidth = 0.3f;

	RagdollMath::GravityFrame mGroundFrame;
	JPH::Vec3 mForward = JPH::Vec3::sAxisZ();
	float32 mOmega = 3.0f;
	float32 mGroundHeight = 0.0f;
	/// Static surface under the foot mGroundHeight came from, which other surfaces are measured against
	float32 mGroundProbe = 0.0f;
	bool mbHasGroundProbe = false;
	/// What the body's height is measured from: mGroundHeight, except that it follows a step up slowly, since the body
	/// takes time to climb after a foot lands up there
	float32 mFallGround = 0.0f;
	bool mbHasGround = false;
	bool mbBodyOnGround = false;

	JPH::Vec3 mCenterOfMass = JPH::Vec3::sZero();
	JPH::Vec3 mVelocity = JPH::Vec3::sZero();
	JPH::Quat mPelvisOrientation = JPH::Quat::sIdentity();
	JPH::Quat mPelvisTarget = JPH::Quat::sIdentity();
	float32 mTrunkTilt = 0.0f;
	JPH::Vec3 mLeanDirection = JPH::Vec3::sAxisZ();
	JPH::Vec3 mCapturePoint = JPH::Vec3::sZero();
	JPH::Vec3 mStaggerVelocity = JPH::Vec3::sZero();
	float32 mStumbleTime = 0.0f;
	/// Time since the first shove of the current stumble (s)
	float32 mReactionElapsed = 0.0f;
	bool mbStumbleStepped = false;

	uint32 mSupportCount = 0;
	JPH::Vec3 mSupportCenter = JPH::Vec3::sZero();
	JPH::Vec3 mSupportTarget = JPH::Vec3::sZero();
	float32 mCaptureOutside = 0.0f;

	Swing mSwing;
	float32 mDoubleSupportTime = 0.0f;
	float32 mLoadWaitTime = 0.0f;
	/// Swings in a row that never reached the ground
	uint32 mFailedSwings = 0;
	/// How far the stance legs bend below their usual height after a landing, easing back out
	float32 mLandingDip = 0.0f;
	float32 mPelvisHeightTarget = 0.0f;
	/// Fraction of body weight the lift carries now, eased so foot contact flicker can't pulse it
	float32 mLift = 0.0f;
	JPH::Vec3 mSmoothedPelvisVelocity = JPH::Vec3::sZero();
	bool mbHasSmoothedPelvisVelocity = false;
	float32 mUnviableTime = 0.0f;
	float32 mDeltaTime = 0.0f;
	float32 mCalmTime = 0.0f;
	float32 mSettleTime = 0.0f;
	float32 mShiftTime = 0.0f;
	int32 mShiftLeg = -1;
	BalanceStatus mStatus;
};

} // namespace fx::physics
