#pragma once

#include <Asset/Animation.hpp>
#include <Core/DynArray.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/Ragdoll.hpp>
#include <memory>

namespace fx {

class Object;

struct NPCDesc
{
	String Name;
	Vec3f Position = Vec3f::sZero;
	float32 Yaw = 0.0f;
	float32 MaxHealth = 100.0f;
	float32 RagdollHealth = 0.0f;
	String IdleAnimation;
};

class NPC
{
public:
	enum class eState : uint8
	{
		Animated,
		Fallen,
		Ragdoll,
	};

public:
	NPC() = default;
	NPC(const NPC&) = delete;
	NPC& operator=(const NPC&) = delete;

	bool Create(const Object* model, const NPCDesc& desc);
	void Destroy();

	void Update(float32 delta_time, bool draw_debug);

	using eBodyPart = physics::eRagdollBodyPart;

	void Damage(float32 amount);
	void Damage(float32 amount, JPH::BodyID body_id);
	eBodyPart GetBodyPart(JPH::BodyID body_id) const;
	void EnterRagdoll();

	void OnObjectHit(const physics::RagdollHit& hit, float32 damage_scale);

	void AddImpulse(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point);
	void React(JPH::BodyID body_id, const Vec3f impulse, const Vec3f point);
	void ActivateBalance();

	void AddWound(JPH::BodyID body_id, const Vec3f point, const Vec3f direction, float32 size);

	void SetPosition(const Vec3f position);
	void SetYaw(float32 yaw);

	void SetIdleAnimation(const String& name, float32 speed = 1.0f, float32 blend_time = Skeleton::scDefaultBlendTime);
	bool PlayAnimation(const String& name, eAnimationEnd on_end = eAnimationEnd::Pop, float32 speed = 1.0f,
					   float32 blend_time = Skeleton::scDefaultBlendTime);

	FX_FORCE_INLINE bool IsCreated() const { return !mRootID.IsInvalid(); }
	FX_FORCE_INLINE bool IsRagdoll() const { return mState == eState::Ragdoll; }
	FX_FORCE_INLINE bool IsFallen() const { return mState == eState::Fallen; }
	FX_FORCE_INLINE eState GetState() const { return mState; }
	FX_FORCE_INLINE float32 GetHealth() const { return mHealth; }
	FX_FORCE_INLINE ObjectID GetRootID() const { return mRootID; }
	FX_FORCE_INLINE const Ref<Skeleton>& GetSkeleton() const { return mpSkeleton; }
	FX_FORCE_INLINE physics::Ragdoll& GetRagdoll() { return mRagdoll; }
	FX_FORCE_INLINE uint32 GetRagdollSerial() const { return mRagdoll.GetSerial(); }

	~NPC() { Destroy(); }

public:
	float32 MaxHealth = 100.0f;
	float32 RagdollHealth = 0.0f;
	float32 BloodCooldown = 0.0f;
	float32 TorsoDamageScale = 1.0f;
	float32 HeadDamageScale = 2.0f;
	float32 LimbDamageScale = 0.5f;
	/// A single hit to the head of at least this much damage drops the NPC outright
	float32 HeadKillDamage = 10.0f;
	float32 ReactStrength = 0.25f;
	float32 ReactRecoverTime = 0.6f;
	float32 Strength = 1.0f;
	float32 RootAssist = 1.0f;
	bool bBalance = true;
	float32 FallStrength = 0.3f;

private:
	Object* GetSkinnedObject() const;
	void UpdateReactions(float32 delta_time);
	void ApplyStrengths();
	void UpdateBalance(float32 delta_time);
	void ReturnToAnimation();
	bool IsPowered() const;
	void SetPowered(bool powered);
	void UpdatePhysicsBlend(float32 delta_time);
	void DrawDebugHealth() const;

	ObjectID mRootID = ObjectID::scNull;
	ObjectID mSkinnedID = ObjectID::scNull;

	Ref<Skeleton> mpSkeleton { nullptr };
	physics::Ragdoll mRagdoll;

	float32 mHealth = 100.0f;
	eState mState = eState::Animated;
	bool mbTeleported = true;
	float32 mBalanceTime = 0.0f;
	float32 mAssistFade = 1.0f;
	float32 mPoweredTime = 0.0f;
	float32 mPhysicsBlend = 1.0f;

	SizedArray<float32> mBodyStrengths;
};

float32 GetHitWoundSize(const physics::RagdollHit& hit);

class NPCManager
{
public:
	NPC* Spawn(const Object* model, const NPCDesc& desc);
	void Destroy(NPC* npc);
	void Clear();
	void ClearRagdolls();

	void Update(float32 delta_time, bool draw_debug);

	NPC* FindByRagdollSerial(uint32 serial);

	float32 ImpactDamageScale = 0.25f;
	float32 MinImpactSpeed = 1.5f;
	float32 ReactStrength = 0.25f;
	float32 Strength = 1.0f;
	float32 RootAssist = 1.0f;
	bool bBalance = true;

	FX_FORCE_INLINE const SizedArray<physics::RagdollHit>& GetHits() const { return mHits; }
	FX_FORCE_INLINE uint32 GetCount() const { return mNPCs.Size; }
	FX_FORCE_INLINE NPC* Get(size_t index) { return mNPCs[index].get(); }

private:
	DynArray<std::unique_ptr<NPC>, GrowthFunctions::Double> mNPCs;
	SizedArray<physics::RagdollHit> mHits;
};

} // namespace fx
