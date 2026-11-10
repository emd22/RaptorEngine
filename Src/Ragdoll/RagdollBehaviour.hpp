#pragma once

#include "RagdollBalance.hpp"
#include "RagdollDef.hpp"
#include "RagdollRig.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Math/Quat.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/FVec3.hpp>

namespace fx::physics {

class Ragdoll;

enum class eRagdollEvent : uint8
{
	Hit,
	Impact,
	LostSupport,
	OnGround,
	Died,
	/// A shove without damage, such as the player walking into the NPC
	Push,
};

struct RagdollEvent
{
	eRagdollEvent Type = eRagdollEvent::Hit;
	int32 Body = -1;
	FVec3 Point = FVec3::sZero;
	FVec3 Direction = FVec3::sZero;
	float32 Magnitude = 0.0f;
	float32 Damage = 0.0f;
};

enum class eBehaviourBalance : uint8
{
	Unset,
	Off,
	On,
};

struct BehaviourBody
{
	FVec3 Origin = FVec3::sZero;
	FVec3 Center = FVec3::sZero;
	FVec3 Velocity = FVec3::sZero;
	FVec3 AngularVelocity = FVec3::sZero;
	JPH::Quat Orientation = JPH::Quat::sIdentity();
	bool bContact = false;
	bool bGround = false;
};

struct BehaviourContext
{
	const Ragdoll* pRagdoll = nullptr;
	const RagdollRig* pRig = nullptr;
	const BalanceStatus* pBalance = nullptr;

	float32 DeltaTime = 0.0f;
	float32 Time = 0.0f;

	SizedArray<BehaviourBody> Bodies;

	FVec3 Up = FVec3::sUp;
	FVec3 CenterOfMass = FVec3::sZero;
	FVec3 Velocity = FVec3::sZero;
	FVec3 Forward = FVec3::sForward;
	FVec3 Right = FVec3::sRight;
	float32 Gravity = 9.81f;

	bool bFeetSupported = false;
	bool bBodyOnGround = false;
	bool bAirborne = false;
	float32 AirTime = 0.0f;
	float32 GroundTime = 0.0f;
	float32 DropHeight = 0.0f;

	bool bImpactPredicted = false;
	FVec3 ImpactPoint = FVec3::sZero;
	FVec3 ImpactNormal = FVec3::sUp;
	float32 ImpactTime = 0.0f;

	bool bFreePose = false;
	bool bBalancing = false;
	bool bAlive = true;

	FX_FORCE_INLINE uint32 GetBodyCount() const { return static_cast<uint32>(Bodies.Size); }
	FX_FORCE_INLINE FVec3 Horizontal(const FVec3 v) const { return v - Up * v.Dot(Up); }
	FX_FORCE_INLINE FVec3 GetChestUp() const { return Forward.Cross(Right); }
	/// Free-falling, or tipping over while balancing before the fall is committed, so impacts can be predicted
	FX_FORCE_INLINE bool IsFalling() const
	{
		return bFreePose || (bBalancing && pBalance != nullptr && pBalance->FallProgress > 0.0f);
	}

	FVec3 ToBodyLocal(uint32 body, const FVec3 point) const;
	FVec3 FromBodyLocal(uint32 body, const FVec3 local) const;
	FVec3 ToBodyDirection(uint32 body, const FVec3 local_direction) const;
	FVec3 ToLocalDirection(uint32 body, const FVec3 direction) const;
	bool IsArmBody(uint32 arm, uint32 body) const;
	int32 FindArm(uint32 body) const;
};

struct BehaviourGoal
{
	FVec3 Target = FVec3::sZero;
	float32 Weight = 0.0f;
};

struct BehaviourOutput
{
	SizedArray<JPH::Quat> Pose;
	SizedArray<float32> Mask;
	SizedArray<float32> Strength;
	BehaviourGoal Hands[RagdollRig::scLimbCount];
	eBehaviourBalance Balance = eBehaviourBalance::Unset;
	float32 GravityScale = 1.0f;

	/// Additive poses rotate on top of the tracked target, override poses replace it
	bool bAdditive = true;

	void Init(uint32 body_count);
	void Reset();

	void AddWorldRotation(const BehaviourContext& ctx, uint32 body, const FVec3 world_axis, float32 angle,
						  float32 mask = 1.0f);
	void ScaleStrength(uint32 body, float32 scale);
	void ScaleAllStrengths(float32 scale);
	void ScaleArmStrengths(const RagdollRig& rig, float32 scale);
};

struct BehaviourSetting
{
	const char* Name = nullptr;
	float32* pValue = nullptr;
};

class RagdollBehaviour
{
public:
	static constexpr uint32 scMaxSettings = 24;

public:
	RagdollBehaviour(const char* name, float32 priority);
	RagdollBehaviour(const RagdollBehaviour&) = delete;
	RagdollBehaviour& operator=(const RagdollBehaviour&) = delete;
	virtual ~RagdollBehaviour() = default;

	virtual void OnEvent(const BehaviourContext& ctx, const RagdollEvent& event) {}
	virtual void Update(const BehaviourContext& ctx) {}
	virtual void Contribute(const BehaviourContext& ctx, BehaviourOutput& out) const = 0;

	virtual void Init(const RagdollRig& rig, uint32 body_count) {}
	virtual void OnStart(const BehaviourContext& ctx) {}
	virtual bool NeedsFreePose() const { return false; }

	void Start(const BehaviourContext& ctx);
	void Stop();
	void Reset();
	void Advance(float32 delta_time);

	bool SetSetting(const char* name, float32 value);
	bool GetSetting(const char* name, float32& out_value) const;
	void CopySettings(const RagdollBehaviour& other);
	void LogSettings() const;

	FX_FORCE_INLINE const char* GetName() const { return mpName; }
	FX_FORCE_INLINE bool IsEnabled() const { return Enabled != 0.0f; }
	FX_FORCE_INLINE bool IsActive() const { return mbActive; }
	FX_FORCE_INLINE bool IsRunning() const { return mbActive || mWeight > 0.0f; }
	FX_FORCE_INLINE float32 GetWeight() const { return mWeight; }
	FX_FORCE_INLINE float32 GetTime() const { return mTime; }

public:
	// Settings are exposed by name to scripts; angles are in degrees and times in seconds

	float32 Enabled = 1.0f;

	/// Behaviours mix in ascending priority, so a higher priority overrides a lower one where they overlap
	float32 Priority = 0.0f;

	float32 FadeIn = 0.1f;
	float32 FadeOut = 0.3f;

protected:
	void AddSetting(const char* name, float32* value);
	void StartIfIdle(const BehaviourContext& ctx);

	float32 mTime = 0.0f;

private:
	const BehaviourSetting* FindSetting(const char* name) const;

	const char* mpName = nullptr;
	BehaviourSetting mSettings[scMaxSettings];
	uint32 mSettingCount = 0;
	float32 mWeight = 0.0f;
	bool mbActive = false;
};

float32 GetBehaviourNoise(float32 time, float32 seed);

} // namespace fx::physics
