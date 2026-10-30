#include "NPCBrain.hpp"

#include <Math/MathUtil.hpp>
#include <cmath>

namespace fx {

namespace {

constexpr float32 scTwoPi = 2.0f * static_cast<float32>(M_PI);

Vec3f Flatten(const Vec3f v) { return Vec3f(v.X, 0.0f, v.Z); }

} // namespace

Vec3f NPCBrain::YawToDirection(float32 yaw) { return Vec3f(std::sin(yaw), 0.0f, std::cos(yaw)); }

float32 NPCBrain::DirectionToYaw(const Vec3f direction) { return std::atan2(direction.X, direction.Z); }

float32 NPCBrain::WrapAngle(float32 angle)
{
	angle = std::fmod(angle + static_cast<float32>(M_PI), scTwoPi);

	if (angle < 0.0f) {
		angle += scTwoPi;
	}

	return angle - static_cast<float32>(M_PI);
}

const char* NPCBrain::GetStateName(eNPCBrainState state)
{
	switch (state) {
	case eNPCBrainState::Idle:
		return "idle";
	case eNPCBrainState::WalkTo:
		return "walk";
	case eNPCBrainState::React:
		return "react";
	case eNPCBrainState::Flee:
		return "flee";
	}

	return "unknown";
}

void NPCBrain::Init(const Vec3f home, uint32 seed)
{
	mHome = home;
	mTarget = home;
	mRandom = seed | 1u;

	Enter(eNPCBrainState::Idle);
}

float32 NPCBrain::RandomUnit()
{
	// Its own sequence, so wandering does not depend on what else draws from the global generator
	mRandom = mRandom * 1664525u + 1013904223u;

	return static_cast<float32>(mRandom >> 8) / static_cast<float32>(1u << 24);
}

void NPCBrain::Enter(eNPCBrainState state)
{
	mState = state;
	mTime = 0.0f;
	mProgressTime = 0.0f;
	mBestDistance = 1e30f;
}

void NPCBrain::Idle() { Enter(eNPCBrainState::Idle); }

void NPCBrain::WalkTo(const Vec3f target)
{
	mTarget = target;
	Enter(eNPCBrainState::WalkTo);
}

void NPCBrain::Flee(const Vec3f from)
{
	mThreat = from;
	mFleeTurn = 1.0f;
	mFleeDirection = Vec3f::sZero;
	Enter(eNPCBrainState::Flee);
}

void NPCBrain::React(const Vec3f source, bool hurt)
{
	// A second hit while reacting restarts the reaction but keeps what to go back to
	if (mState == eNPCBrainState::React) {
		mbHurt = mbHurt || hurt;
	}
	else {
		mResumeState = mState;
		mResumeTarget = mTarget;
		mbHurt = hurt || mState == eNPCBrainState::Flee;
	}

	mThreat = source;

	Enter(eNPCBrainState::React);
}

void NPCBrain::TurnTowards(float32 delta_time, float32 yaw, NPCBrainOutput& out) const
{
	const float32 max_turn = MathUtil::DegToRad(TurnRate) * delta_time;
	const float32 error = WrapAngle(yaw - out.Yaw);

	out.Yaw = WrapAngle(out.Yaw + MathUtil::Clamp(error, -max_turn, max_turn));
}

bool NPCBrain::SteerTowards(float32 delta_time, const NPCBrainInput& input, const Vec3f target, float32 speed,
							NPCBrainOutput& out)
{
	const Vec3f offset = Flatten(target - input.Position);
	const float32 distance = offset.Length();

	if (distance <= ArriveRadius) {
		return true;
	}

	const float32 wanted_yaw = DirectionToYaw(offset);
	TurnTowards(delta_time, wanted_yaw, out);

	// Walk along the facing, slowing while the turn is still large, so the feet follow where the body points
	const float32 error = std::abs(WrapAngle(wanted_yaw - out.Yaw));
	const float32 turn_limit = MathUtil::DegToRad(TurnInPlaceAngle);
	const float32 turn_scale = MathUtil::Clamp(1.0f - error / turn_limit, 0.0f, 1.0f);
	const float32 arrive_scale = MathUtil::Clamp(distance / SlowRadius, 0.3f, 1.0f);

	out.Velocity = YawToDirection(out.Yaw) * (speed * turn_scale * arrive_scale);

	return false;
}

bool NPCBrain::CheckStuck(float32 delta_time, const NPCBrainInput& input, const Vec3f target)
{
	const float32 distance = Flatten(target - input.Position).Length();

	// Progress is measured from a checkpoint, so steady slow walking counts as long as it adds up within StuckTime
	if (distance < mBestDistance - StuckDistance) {
		mBestDistance = distance;
		mProgressTime = 0.0f;
		return false;
	}

	mProgressTime += delta_time;

	return mProgressTime >= StuckTime;
}

void NPCBrain::PickFleeTarget(const Vec3f position, float32 turn)
{
	Vec3f away = Flatten(position - mThreat);

	if (away.Length() < 1e-3f) {
		away = YawToDirection(RandomUnit() * scTwoPi);
	}

	const float32 yaw = DirectionToYaw(away) + turn;

	mFleeDirection = YawToDirection(yaw);
	mTarget = position + mFleeDirection * FleeDistance;
	mProgressTime = 0.0f;
	mBestDistance = 1e30f;
}

void NPCBrain::Update(float32 delta_time, const NPCBrainInput& input, NPCBrainOutput& out)
{
	constexpr float32 scFleeReplanTurn = MathUtil::DegToRad(70.0f);

	out.Velocity = Vec3f::sZero;
	out.Yaw = input.Yaw;

	mTime += delta_time;

	switch (mState) {
	case eNPCBrainState::Idle:
		if (WanderRadius > 0.0f && mTime >= WanderPause) {
			const float32 angle = RandomUnit() * scTwoPi;
			const float32 radius = WanderRadius * std::sqrt(RandomUnit());

			WalkTo(mHome + YawToDirection(angle) * radius);
		}
		break;

	case eNPCBrainState::WalkTo:
		if (SteerTowards(delta_time, input, mTarget, WalkSpeed, out) || CheckStuck(delta_time, input, mTarget)) {
			Idle();
		}
		break;

	case eNPCBrainState::React:
		// A hurt NPC turns away from the shot rather than facing it
		if (mbHurt) {
			TurnTowards(delta_time, DirectionToYaw(Flatten(input.Position - mThreat)), out);
		}
		else {
			TurnTowards(delta_time, DirectionToYaw(Flatten(mThreat - input.Position)), out);
		}

		if (mTime < ReactTime) {
			break;
		}

		if (mbHurt) {
			Flee(mThreat);
		}
		else if (mResumeState == eNPCBrainState::WalkTo) {
			WalkTo(mResumeTarget);
		}
		else {
			Idle();
		}
		break;

	case eNPCBrainState::Flee:
		if (mFleeDirection.Length() < 0.5f) {
			PickFleeTarget(input.Position, 0.0f);
		}

		if (mTime >= FleeTime) {
			mHome = input.Position;
			Idle();
			break;
		}

		if (SteerTowards(delta_time, input, mTarget, RunSpeed, out)) {
			PickFleeTarget(input.Position, 0.0f);
		}
		else if (CheckStuck(delta_time, input, mTarget)) {
			// Blocked: try off to one side, alternating, as there is no path finding to go round the obstacle
			PickFleeTarget(input.Position, scFleeReplanTurn * mFleeTurn);
			mFleeTurn = -mFleeTurn * 1.5f;

			if (std::abs(mFleeTurn) > 3.0f) {
				mFleeTurn = 1.0f;
			}
		}
		break;
	}
}

} // namespace fx
