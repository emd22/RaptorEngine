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
#include <Ragdoll/Ragdoll.hpp>
#include <Ragdoll/RagdollBehaviours.hpp>
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

struct NPCHit
{
	JPH::BodyID Body {};
	Vec3f Point = Vec3f::sZero;
	Vec3f Impulse = Vec3f::sZero;
	float32 Damage = 0.0f;
};

/// Settings shared by every NPC and ragdoll dummy, driven by the b_npc_* and b_ragdoll_* cvars
struct NPCTuning
{
	float32 Strength = 1.0f;
	float32 RootAssist = 1.0f;
	bool bBalance = true;
	float32 ImpactDamageScale = 0.25f;
	float32 MinImpactSpeed = 1.5f;
	float32 SafeImpactSpeed = 6.0f;
	float32 BloodSpeed = 3.5f;
};

/**
 * @brief A skinned model cloned with its own skeleton plus the physics ragdoll that poses it used by NPCs
 */
class RagdollEntity
{
public:
	RagdollEntity() = default;
	RagdollEntity(const RagdollEntity&) = delete;
	RagdollEntity& operator=(const RagdollEntity&) = delete;

	bool Clone(const Object* model, const String& name);
	bool CreateRagdoll();
	void Destroy();

	void SetCullable(bool cullable);
	bool AddWound(JPH::BodyID body_id, const Vec3f point, const Vec3f direction, float32 size);
	void AddContactWound(const physics::RagdollContact& contact);
	void TickCooldown(float32 delta_time);

	Object* GetRoot() const;
	Object* GetSkinned() const;

	FX_FORCE_INLINE bool IsCreated() const { return !RootID.IsInvalid(); }
	FX_FORCE_INLINE bool OwnsContact(const physics::RagdollContact& contact) const
	{
		return Ragdoll.IsCreated() && Ragdoll.GetSerial() == contact.RagdollSerial;
	}

	~RagdollEntity() { Destroy(); }

public:
	physics::Ragdoll Ragdoll;
	Ref<Skeleton> pSkeleton { nullptr };
	ObjectID RootID = ObjectID::scNull;
	ObjectID SkinnedID = ObjectID::scNull;

	/// Shared by skinned wounds and world blood splats so one impact does not spray every frame
	float32 BloodCooldown = 0.0f;
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

	bool Create(const Object* model, const NPCDesc& desc, const NPCTuning& tuning);
	void Destroy();

	void Update(float32 delta_time, bool draw_debug);

	using eBodyPart = physics::eRagdollBodyPart;

	void Damage(float32 amount);
	void Damage(float32 amount, JPH::BodyID body_id);
	eBodyPart GetBodyPart(JPH::BodyID body_id) const;
	float32 GetDamageScale(JPH::BodyID body_id) const;
	void Kill(int32 body = -1);

	void OnContact(const physics::RagdollContact& contact);
	float32 GetContactDamage(const physics::RagdollContact& contact) const;

	void OnHit(const NPCHit& hit);

	bool TriggerBehaviour(const char* name);
	bool StopBehaviour(const char* name);

	void AddWound(JPH::BodyID body_id, const Vec3f point, const Vec3f direction, float32 size);

	void SetPosition(const Vec3f position);
	void SetYaw(float32 yaw);

	void SetIdleAnimation(const String& name, float32 speed = 1.0f, float32 blend_time = Skeleton::scDefaultBlendTime);
	bool PlayAnimation(const String& name, eAnimationEnd on_end = eAnimationEnd::Pop, float32 speed = 1.0f,
					   float32 blend_time = Skeleton::scDefaultBlendTime);

	FX_FORCE_INLINE bool IsCreated() const { return mActor.IsCreated(); }
	FX_FORCE_INLINE bool IsRagdoll() const { return mState == eState::Ragdoll; }
	FX_FORCE_INLINE bool IsFallen() const { return mState == eState::Fallen; }
	FX_FORCE_INLINE bool IsDying() const { return mbDying; }
	FX_FORCE_INLINE bool IsAlive() const { return !mbDying && mState != eState::Ragdoll; }
	FX_FORCE_INLINE const String& GetName() const { return mName; }
	FX_FORCE_INLINE eState GetState() const { return mState; }
	FX_FORCE_INLINE float32 GetHealth() const { return mHealth; }
	FX_FORCE_INLINE ObjectID GetRootID() const { return mActor.RootID; }
	FX_FORCE_INLINE const Ref<Skeleton>& GetSkeleton() const { return mActor.pSkeleton; }
	FX_FORCE_INLINE physics::Ragdoll& GetRagdoll() { return mActor.Ragdoll; }
	FX_FORCE_INLINE uint32 GetRagdollSerial() const { return mActor.Ragdoll.GetSerial(); }
	FX_FORCE_INLINE physics::RagdollBehaviours& GetBehaviours() { return mBehaviours; }
	FX_FORCE_INLINE RagdollEntity& GetActor() { return mActor; }

	~NPC() { Destroy(); }

public:
	float32 MaxHealth = 100.0f;
	float32 RagdollHealth = 0.0f;
	float32 TorsoDamageScale = 1.0f;
	float32 HeadDamageScale = 2.0f;
	float32 LimbDamageScale = 0.5f;
	/// A single hit to the head of at least this much damage drops the NPC outright
	float32 HeadKillDamage = 10.0f;

private:
	FX_FORCE_INLINE bool CanTakeDamage() const { return IsCreated() && IsAlive(); }

	void ApplyDamage(float32 amount, int32 body);
	void UpdateActive(float32 delta_time);
	void UpdateBehaviours(float32 delta_time);
	void ApplyStrengths();
	void ApplyBehaviourOutput();
	void UpdateBalance(float32 delta_time);
	void ActivateBalance();
	void ReturnToAnimation();
	void EnterFallen();
	void EnterRagdoll();
	bool IsPowered() const;
	void SetPowered(bool powered);
	void WakePhysics();
	void UpdatePhysicsBlend(float32 delta_time);
	void DrawDebugHealth() const;

	RagdollEntity mActor;
	physics::RagdollBehaviours mBehaviours;
	const NPCTuning* mpTuning = nullptr;
	String mName;

	float32 mHealth = 100.0f;
	eState mState = eState::Animated;
	bool mbDying = false;
	bool mbTeleported = true;
	float32 mAssistFade = 1.0f;
	float32 mPoweredTime = 0.0f;
	float32 mPhysicsBlend = 1.0f;
	float32 mPendingImpactDamage = 0.0f;
};

class NPCManager
{
public:
	NPC* Spawn(const Object* model, const NPCDesc& desc);
	void Destroy(NPC* npc);
	void Clear();
	void ClearRagdolls();

	bool SpawnDummy(const Object* model, const String& name, const Vec3f position, const Vec3f velocity);
	void DestroyDummy(uint32 index);
	void ClearDummies();

	void Update(float32 delta_time, bool draw_debug);

	NPC* FindByRagdollSerial(uint32 serial);
	NPC* FindByName(const char* name);

	bool SetBehaviourSetting(uint32 serial, const char* behaviour, const char* setting, float32 value);
	bool GetBehaviourSetting(uint32 serial, const char* behaviour, const char* setting, float32& out_value);

	FX_FORCE_INLINE const SizedArray<physics::RagdollContact>& GetContacts() const { return mContacts; }
	FX_FORCE_INLINE uint32 GetCount() const { return mNPCs.Size; }
	FX_FORCE_INLINE uint32 GetDummyCount() const { return mDummies.Size; }
	FX_FORCE_INLINE NPC* Get(size_t index) { return mNPCs[index].get(); }
	FX_FORCE_INLINE RagdollEntity* GetDummy(size_t index) { return mDummies[index].get(); }
	FX_FORCE_INLINE physics::RagdollBehaviours& GetDefaultBehaviours() { return mDefaultBehaviours; }

public:
	NPCTuning Tuning;

private:
	RagdollEntity* FindContactOwner(const physics::RagdollContact& contact, bool& out_is_npc);
	void SpawnWorldBlood(RagdollEntity& actor, const physics::RagdollContact& contact) const;

	DynArray<std::unique_ptr<NPC>, GrowthFunctions::Double> mNPCs;
	DynArray<std::unique_ptr<RagdollEntity>, GrowthFunctions::Double> mDummies;
	SizedArray<physics::RagdollContact> mContacts;
	physics::RagdollBehaviours mDefaultBehaviours;
};

} // namespace fx
