#pragma once

#include <Core/Types.hpp>
#include <Math/FVec3.hpp>

namespace fx {

enum class eNPCBrainState : uint8
{
	Idle,
	WalkTo,
	React,
	Flee,
};

struct NPCBrainInput
{
	FVec3 Position = FVec3::sZero;
	float32 Yaw = 0.0f;
};

struct NPCBrainOutput
{
	FVec3 Velocity = FVec3::sZero;
	float32 Yaw = 0.0f;
};

/**
 * @brief Picks where an animated NPC walks and which way it faces. Steering is straight-line with a turn rate limit;
 * there is no path finding, so an NPC that stops making progress gives up.
 */
class NPCBrain
{
public:
	static FVec3 YawToDirection(float32 yaw);
	static float32 DirectionToYaw(const FVec3 direction);
	static float32 WrapAngle(float32 angle);

	static const char* GetStateName(eNPCBrainState state);

	void Init(const FVec3 home, uint32 seed);

	void Idle();
	void WalkTo(const FVec3 target);
	void Flee(const FVec3 from);
	/// Stops and faces `source`, then flees from it if `hurt`, otherwise carries on with what it was doing
	void React(const FVec3 source, bool hurt);

	void Update(float32 delta_time, const NPCBrainInput& input, NPCBrainOutput& out);

	FX_FORCE_INLINE eNPCBrainState GetState() const { return mState; }
	FX_FORCE_INLINE const FVec3& GetTarget() const { return mTarget; }
	FX_FORCE_INLINE void SetHome(const FVec3 home) { mHome = home; }

public:
	/// Metres per second
	float32 WalkSpeed = 1.4f;
	float32 RunSpeed = 3.5f;
	/// Degrees per second
	float32 TurnRate = 240.0f;
	/// The NPC stops moving forward while it faces further than this from where it wants to go (degrees)
	float32 TurnInPlaceAngle = 90.0f;

	float32 ArriveRadius = 0.35f;
	float32 SlowRadius = 1.0f;

	float32 ReactTime = 1.2f;
	float32 FleeTime = 6.0f;
	float32 FleeDistance = 12.0f;

	/// Idle NPCs walk to a random point this far from home every so often. 0 stands still
	float32 WanderRadius = 0.0f;
	float32 WanderPause = 3.0f;

	/// Gives up on a target after this long without getting `StuckDistance` closer
	float32 StuckTime = 2.0f;
	float32 StuckDistance = 0.25f;

private:
	void Enter(eNPCBrainState state);
	bool SteerTowards(float32 delta_time, const NPCBrainInput& input, const FVec3 target, float32 speed,
					  NPCBrainOutput& out);
	void TurnTowards(float32 delta_time, float32 yaw, NPCBrainOutput& out) const;
	bool CheckStuck(float32 delta_time, const NPCBrainInput& input, const FVec3 target);
	void PickFleeTarget(const FVec3 position, float32 turn);
	float32 RandomUnit();

	eNPCBrainState mState = eNPCBrainState::Idle;
	eNPCBrainState mResumeState = eNPCBrainState::Idle;

	FVec3 mHome = FVec3::sZero;
	FVec3 mTarget = FVec3::sZero;
	FVec3 mResumeTarget = FVec3::sZero;
	FVec3 mThreat = FVec3::sZero;
	FVec3 mFleeDirection = FVec3::sZero;

	float32 mTime = 0.0f;
	float32 mBestDistance = 0.0f;
	float32 mProgressTime = 0.0f;
	float32 mFleeTurn = 1.0f;
	bool mbHurt = false;

	uint32 mRandom = 1;
};

} // namespace fx
