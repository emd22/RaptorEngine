#pragma once

#include "Ragdoll.hpp"
#include "RagdollReactions.hpp"

namespace fx::physics {

class RagdollBehaviours
{
public:
	static constexpr uint32 scBehaviourCount = 8;

public:
	RagdollBehaviours();
	RagdollBehaviours(const RagdollBehaviours&) = delete;
	RagdollBehaviours& operator=(const RagdollBehaviours&) = delete;

	bool Init(Ragdoll& ragdoll);
	void Reset();
	void CopySettings(const RagdollBehaviours& other);

	void HandleEvent(Ragdoll& ragdoll, const RagdollEvent& event);
	void Update(Ragdoll& ragdoll, float32 delta_time);

	void SetFreePose(bool free_pose);
	void SetAlive(bool alive);

	RagdollBehaviour* Find(const char* name);
	const RagdollBehaviour* Find(const char* name) const;
	bool Trigger(Ragdoll& ragdoll, const char* name);
	bool Stop(const char* name);
	bool IsRunning(const char* name) const;
	void LogState() const;

	bool IsIdle() const;
	bool HadEvent(eRagdollEvent type) const;

	void DebugDraw() const;

	FX_FORCE_INLINE bool IsValid() const { return mpRig != nullptr && mpRig->bValid; }
	FX_FORCE_INLINE float32 GetStrength(uint32 body) const
	{
		if (body >= mMixStrength.Size) {
			return 1.0f;
		}

		return mMixStrength[body];
	}
	FX_FORCE_INLINE eBehaviourBalance GetBalance() const { return mMixBalance; }
	FX_FORCE_INLINE bool IsDying() const { return mDying.IsActive(); }
	FX_FORCE_INLINE bool IsDeathComplete() const { return mDying.IsFinished(); }
	FX_FORCE_INLINE bool IsFreePose() const { return mContext.bFreePose; }
	FX_FORCE_INLINE const BehaviourContext& GetContext() const { return mContext; }

	FX_FORCE_INLINE uint32 GetBehaviourCount() const { return scBehaviourCount; }
	FX_FORCE_INLINE const RagdollBehaviour* GetBehaviour(uint32 index) const { return mBehaviours[index]; }

private:
	bool CanRun(const Ragdoll& ragdoll) const;
	int32 FindIndex(const char* name) const;

	void Sense(Ragdoll& ragdoll, float32 delta_time);
	void SenseChestFrame(const Ragdoll& ragdoll);
	void PredictImpact();
	void EmitSupportEvents(float32 delta_time);
	void Dispatch(const RagdollEvent& event);
	void SortByPriority();

	void Resolve(Ragdoll& ragdoll);
	void ClearMix();
	void Mix();
	void FoldOutput(const BehaviourOutput& output, float32 weight);
	void FoldOverride(uint32 body, const JPH::Quat& pose, float32 weight);
	void SolveArm(uint32 arm, const BehaviourGoal& goal, float32 weight);
	void Apply(Ragdoll& ragdoll) const;

private:
	FallenBehaviour mFallen;
	StaggerBehaviour mStagger;
	ShotReactionBehaviour mShot;
	BraceBehaviour mBrace;
	ProtectHeadBehaviour mProtectHead;
	HighFallBehaviour mHighFall;
	WritheBehaviour mWrithe;
	DyingBehaviour mDying;

	RagdollBehaviour* mBehaviours[scBehaviourCount];
	RagdollBehaviour* mOrder[scBehaviourCount];

	const RagdollRig* mpRig = nullptr;
	BehaviourContext mContext;
	SizedArray<Ragdoll::BodyState> mSamples;
	BehaviourOutput mOutput;

	/// The mixed pose layers, copied into the ragdoll by Apply (GravityScale comes from mMixGravity)
	SizedArray<Ragdoll::BodyLayer> mMixLayers;
	SizedArray<float32> mMixStrength;
	eBehaviourBalance mMixBalance = eBehaviourBalance::Unset;
	float32 mMixGravity = 1.0f;
	BehaviourGoal mMixGoals[RagdollRig::scLimbCount];

	float32 mUnsupportedTime = 0.0f;
	float32 mOffGroundTime = 0.0f;
	bool mbLostSupportSent = false;
	bool mbOnGroundSent = false;
	uint32 mFrameEvents = 0;
};

} // namespace fx::physics
