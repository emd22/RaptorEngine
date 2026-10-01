#pragma once

#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Script/Script.hpp>

namespace fx {

class Player;

enum class eWeaponFireMode : int32
{
	None = (0),
	Semi = (1 << 0),
	Auto = (1 << 1),
	Burst = (1 << 2),
	Count = 3,
};
FxEnumFlags(eWeaponFireMode);

enum class eWeaponInputFlags : int32
{
	None = (0),
	FirePressed = (1 << 0),
	FireHeld = (1 << 1),
	ReloadPressed = (1 << 2),
	SwitchMode = (1 << 3),
	SwitchWeapon = (1 << 4),
	Airborne = (1 << 5),
};

FxEnumFlags(eWeaponInputFlags);

namespace weapon {

static constexpr uint32 scMaxWeapons = 8;

enum class ePhase : int32
{
	Ready,
	Reloading,
	Raising,
	Lowering,
	Lowered,
};


enum class eEvent : int32
{
	Fire,
	DryFire,
	ReloadBegin,
	ReloadEnd,
	Raise,
	Lower,
	ModeChanged,
};

struct ScriptDef
{
	float32 Damage = 0.0f;
	float32 Range = 0.0f;
	float32 RoundsPerMinute = 0.0f;
	int32 Pellets = 1;

	eWeaponFireMode FireModeFlags = eWeaponFireMode::Semi;
	eWeaponFireMode DefaultMode = eWeaponFireMode::Semi;

	int32 BurstCount = 1;
	float32 BurstRoundsPerMinute = 0.0f;
	float32 BurstCooldown = 0.0f;
	float32 HitForce = 0.0f;
	int32 Decals = 0;

	float32 SpreadHip = 0.0f;
	float32 SpreadMoving = 0.0f;
	float32 SpreadAir = 0.0f;
	float32 SpreadPerShot = 0.0f;
	float32 SpreadMax = 0.0f;
	float32 SpreadRecovery = 0.0f;

	float32 RecoilPitch = 0.0f;
	float32 RecoilPitchVariance = 0.0f;
	float32 RecoilYaw = 0.0f;
	float32 RecoilRamp = 0.0f;
	float32 RecoilRampMax = 0.0f;
	float32 RecoilRecovery = 0.0f;
	float32 RecoilResetTime = 0.0f;

	float32 FalloffStart = 0.0f;
	float32 FalloffEnd = 0.0f;
	float32 FalloffMin = 1.0f;

	int32 MagazineSize = 0;
	int32 ReserveMax = 0;
	int32 ReserveStart = 0;
	float32 ReloadTime = 0.0f;
	float32 ReloadEmptyTime = 0.0f;
	int32 AutoReload = 0;

	float32 RaiseTime = 0.0f;
	float32 LowerTime = 0.0f;
};

struct ScriptState
{
	ePhase Phase = ePhase::Ready;
	eWeaponFireMode FireMode = eWeaponFireMode::None;

	int32 Magazine = 0;
	int32 Reserve = 0;
	int32 BurstRemaining = 0;
	int32 RecoilShots = 0;

	float32 Timer = 0.0f;
	float32 TimerTotal = 1.0f;
	float32 Cooldown = 0.0f;
	float32 SinceShot = 0.0f;
	float32 Bloom = 0.0f;
	float32 FireBuffer = 0.0f;
};

struct ScriptInput
{
	eWeaponInputFlags InputFlags = eWeaponInputFlags::None;
	float32 MoveAmount = 0.0f;
};

static_assert(sizeof(ScriptDef) == 35 * 4);
static_assert(sizeof(ScriptState) == 12 * 4);

using FnInit = script::ScriptFunctionType<void(const ScriptDef*, ScriptState*)>;
using FnUpdate = script::ScriptFunctionType<void(const ScriptDef*, ScriptState*, const ScriptInput*, float32)>;

struct Weapon
{
	String Name;
	int32 Slot = 0;
	String ScriptPath;

	String IdleAnim;
	String FireAnim;
	String ReloadAnim;

	float32 ViewKickDegrees = 2.5f;
	float32 ViewKickback = 0.025f;

	ScriptDef Def;
	ScriptState State;

	script::Script* pScript = nullptr;
	FnInit pFnInit = nullptr;
	FnInit pFnEquip = nullptr;
	FnInit pFnHolster = nullptr;
	FnUpdate pFnUpdate = nullptr;
};


class WeaponSystem
{
public:
	WeaponSystem() = default;

	void Create(Player* player);

	void SetEnabled(bool enabled) { mbEnabled = enabled; }
	void SetInput(const eWeaponInputFlags input_flags) { mScriptInput.InputFlags = input_flags; }

	void Update(float32 delta_time);
	void RenderHud();

	void OnScriptsReloaded();

	void SelectWeapon(uint32 index);
	void SwitchToNextWeapon();

	uint32 GetWeaponCount() const { return mWeaponCount; }
	const Weapon* GetActiveWeapon() const { return mWeaponCount > 0 ? &mWeapons[mActive] : nullptr; }

	void HandleEvent(eEvent event);
	float32 Trace(float32 yaw, float32 pitch, float32 range);
	void ApplyHit(float32 damage, float32 force, bool decal);
	void Kick(float32 pitch, float32 yaw);

private:
	bool LoadWeapon(const char* path, Weapon& weapon);
	void BindFunctions(Weapon& weapon);
	void ApplyAnimations(const Weapon& weapon);
	void UpdateSwitching();
	void UpdateHolsterAmount();

private:
	Weapon mWeapons[scMaxWeapons];
	uint32 mWeaponCount = 0;
	uint32 mActive = 0;
	uint32 mPending = 0;

	Player* mpPlayer = nullptr;

	ScriptInput mScriptInput {};

	bool mbFireLatched = false;
	bool mbSwitching = false;
	bool mbEnabled = true;

	physics::RayResult mLastHit;
	Vec3f mLastDirection = Vec3f::sZero;
};

} // namespace weapon

} // namespace fx
