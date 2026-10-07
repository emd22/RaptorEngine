#include "WeaponSystem.hpp"

#include <Asset/ConfigFile.hpp>
#include <CVar.hpp>
#include <Color.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Core/Random.hpp>
#include <Decal/DecalManager.hpp>
#include <Engine.hpp>
#include <Math/MathUtil.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Player.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/TextRenderer.hpp>
#include <Script/ScriptManager.hpp>
#include <World.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <utility>

namespace fx::weapon {

using namespace renderer;

static constexpr const char* scWeaponDirectory = "RaptorData/Data/Weapons";
static constexpr float32 scSprintSpeed = 5.0f;

namespace {

bool EqualsIgnoreCase(const char* a, const char* b)
{
	for (; *a != '\0' && *b != '\0'; ++a, ++b) {
		if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
			return false;
		}
	}

	return *a == *b;
}

eWeaponFireMode ModeFromName(const char* name)
{
	if (EqualsIgnoreCase(name, "Semi")) {
		return eWeaponFireMode::Semi;
	}
	if (EqualsIgnoreCase(name, "Auto")) {
		return eWeaponFireMode::Auto;
	}
	if (EqualsIgnoreCase(name, "Burst")) {
		return eWeaponFireMode::Burst;
	}

	return eWeaponFireMode::None;
}

const char* ModeName(eWeaponFireMode mode)
{
	switch (static_cast<eWeaponFireMode>(mode)) {
	case eWeaponFireMode::Semi:
		return "SEMI";
	case eWeaponFireMode::Auto:
		return "AUTO";
	case eWeaponFireMode::Burst:
		return "BURST";
	default:
		return "";
	}
}

const char* EventName(eEvent event)
{
	switch (event) {
	case eEvent::Fire:
		return "fire";
	case eEvent::DryFire:
		return "dry fire";
	case eEvent::ReloadBegin:
		return "reload begin";
	case eEvent::ReloadEnd:
		return "reload end";
	case eEvent::Raise:
		return "raise";
	case eEvent::Lower:
		return "lower";
	case eEvent::ModeChanged:
		return "mode changed";
	}

	return "";
}

const ConfigEntry* Child(const ConfigEntry* parent, const char* name)
{
	return parent != nullptr ? parent->GetMember(HashStr32(name)) : nullptr;
}

template <typename T>
T Member(const ConfigEntry* parent, const char* name, T fallback)
{
	if (parent == nullptr) {
		return fallback;
	}

	return parent->GetMemberValue<T>(HashStr32(name), fallback);
}

String StringMember(const ConfigEntry* parent, const char* name, const char* fallback)
{
	return String(Member<const char*>(parent, name, fallback));
}

eWeaponFireMode ParseModes(const ConfigEntry* fire, eWeaponFireMode& out_default_mode)
{
	eWeaponFireMode mode_flags = eWeaponFireMode::None;
	out_default_mode = eWeaponFireMode::None;

	const ConfigEntry* entry = Child(fire, "modes");

	if (entry != nullptr && entry->bIsArray) {
		for (const ConfigPrimitive& primitive : entry->GetArrayData()) {
			const char* name = primitive.Get<const char*>();

			eWeaponFireMode mode = ModeFromName(name);
			mode_flags |= (mode);

			// If there has not been a mode set, set it to the currently found one.
			// This means that the first mode defined in the list will be the default mode for the weapon.
			if (out_default_mode == eWeaponFireMode::None) {
				out_default_mode = mode;
			}
		}
	}

	if (mode_flags == eWeaponFireMode::None) {
		mode_flags = eWeaponFireMode::Semi;
		out_default_mode = eWeaponFireMode::Semi;
	}

	return mode_flags;
}

uint32 PackColor(uint8 r, uint8 g, uint8 b) { return Color::FromRGBA(r, g, b, 255).AsUInt(); }

} // namespace

bool WeaponSystem::LoadWeapon(const char* path, Weapon& weapon)
{
	ConfigFile config;
	config.Load(path);

	if (config.HasErrors()) {
		LogError(LC_SCRIPT, "Could not parse weapon config '{}'", path);
		return false;
	}

	const ConfigEntry* script_entry = config.GetEntry(HashStr32("script"));
	if (script_entry == nullptr) {
		LogError(LC_SCRIPT, "Weapon config '{}' has no Script entry", path);
		return false;
	}

	const ConfigEntry* name_entry = config.GetEntry(HashStr32("name"));
	const ConfigEntry* slot_entry = config.GetEntry(HashStr32("slot"));
	const ConfigEntry* animations = config.GetEntry(HashStr32("animations"));
	const ConfigEntry* fire = config.GetEntry(HashStr32("fire"));
	const ConfigEntry* spread = config.GetEntry(HashStr32("spread"));
	const ConfigEntry* recoil = config.GetEntry(HashStr32("recoil"));
	const ConfigEntry* falloff = config.GetEntry(HashStr32("falloff"));
	const ConfigEntry* ammo = config.GetEntry(HashStr32("ammo"));
	const ConfigEntry* switching = config.GetEntry(HashStr32("switch"));

	weapon.ScriptPath = String(script_entry->GetValue<const char*>());
	weapon.Name = name_entry != nullptr ? String(name_entry->GetValue<const char*>()) : String("Weapon");
	weapon.Slot = slot_entry != nullptr ? slot_entry->GetValue<int32>() : 0;

	weapon.IdleAnim = StringMember(animations, "idle", "BASE");
	weapon.FireAnim = StringMember(animations, "fire", "Armature|Fire");
	weapon.ReloadAnim = StringMember(animations, "reload", "Armature|ReloadClip");

	ScriptDef& def = weapon.Def;

	def.Damage = Member<float32>(fire, "damage", 20.0f);
	def.Range = Member<float32>(fire, "range", 100.0f);
	def.RoundsPerMinute = Member<float32>(fire, "rounds_per_minute", 600.0f);
	def.Pellets = std::max(Member<int32>(fire, "pellets", 1), 1);
	def.FireModeFlags = ParseModes(fire, def.DefaultMode);
	def.BurstCount = std::max(Member<int32>(fire, "burst_count", 3), 1);
	def.BurstRoundsPerMinute = Member<float32>(fire, "burst_rounds_per_minute", def.RoundsPerMinute);
	def.BurstCooldown = Member<float32>(fire, "burst_cooldown", 0.0f);
	def.HitForce = Member<float32>(fire, "hit_force", 0.0f);
	def.Decals = Member<int32>(fire, "decals", 1);

	def.SpreadHip = Member<float32>(spread, "hip", 0.0f);
	def.SpreadMoving = Member<float32>(spread, "moving", 0.0f);
	def.SpreadAir = Member<float32>(spread, "air", 0.0f);
	def.SpreadPerShot = Member<float32>(spread, "per_shot", 0.0f);
	def.SpreadMax = Member<float32>(spread, "max", 0.0f);
	def.SpreadRecovery = Member<float32>(spread, "recovery", 0.0f);

	def.RecoilPitch = Member<float32>(recoil, "pitch", 0.0f);
	def.RecoilPitchVariance = Member<float32>(recoil, "pitch_variance", 0.0f);
	def.RecoilYaw = Member<float32>(recoil, "yaw", 0.0f);
	def.RecoilRamp = Member<float32>(recoil, "ramp", 0.0f);
	def.RecoilRampMax = Member<float32>(recoil, "ramp_max", 0.0f);
	def.RecoilRecovery = Member<float32>(recoil, "recovery", 0.0f);
	def.RecoilResetTime = Member<float32>(recoil, "reset_time", 0.25f);
	weapon.ViewKickback = Member<float32>(recoil, "view_kick", 0.25f);

	def.FalloffStart = Member<float32>(falloff, "start", def.Range);
	def.FalloffEnd = Member<float32>(falloff, "end", def.Range);
	def.FalloffMin = Member<float32>(falloff, "min_multiplier", 1.0f);

	def.MagazineSize = std::max(Member<int32>(ammo, "magazine_size", 1), 1);
	def.ReserveMax = std::max(Member<int32>(ammo, "reserve_max", 0), 0);
	def.ReserveStart = std::clamp(Member<int32>(ammo, "reserve_start", def.ReserveMax), 0, def.ReserveMax);
	def.ReloadTime = Member<float32>(ammo, "reload_time", 2.0f);
	def.ReloadEmptyTime = Member<float32>(ammo, "reload_empty_time", def.ReloadTime);
	def.AutoReload = Member<int32>(ammo, "auto_reload", 1);

	def.RaiseTime = Member<float32>(switching, "raise_time", 0.4f);
	def.LowerTime = Member<float32>(switching, "lower_time", 0.3f);

	weapon.State.FireMode = def.DefaultMode;
	weapon.State.Magazine = def.MagazineSize;
	weapon.State.Reserve = def.ReserveStart;

	return true;
}

void WeaponSystem::BindFunctions(Weapon& weapon)
{
	if (weapon.pScript == nullptr) {
		weapon.pFnInit = nullptr;
		weapon.pFnEquip = nullptr;
		weapon.pFnHolster = nullptr;
		weapon.pFnUpdate = nullptr;
		return;
	}

	using Signature = void(const ScriptDef*, ScriptState*);

	weapon.pFnInit = weapon.pScript->GetFunction<Signature>("weapon_init");
	weapon.pFnEquip = weapon.pScript->GetFunction<Signature>("weapon_equip");
	weapon.pFnHolster = weapon.pScript->GetFunction<Signature>("weapon_holster");
	weapon.pFnUpdate = weapon.pScript->GetFunction<void(const ScriptDef*, ScriptState*, const ScriptInput*, float32)>(
		"weapon_update");

	if (weapon.pFnUpdate == nullptr) {
		LogError(LC_SCRIPT, "Weapon '{}' ({}) has no weapon_update function", weapon.Name, weapon.ScriptPath);
	}
}

void WeaponSystem::Create(Player* player)
{
	mpPlayer = player;
	mWeaponCount = 0;
	mActive = 0;
	mPending = 0;
	mbSwitching = false;

	gCVars->Set("b_weapon_debug", false);

	const std::string directory = FilesystemIO::ResolvePath(scWeaponDirectory);

	if (!std::filesystem::exists(directory)) {
		LogWarning(LC_SCRIPT, "Weapon directory '{}' does not exist", directory);
		return;
	}

	PagedArray<std::string> files = FilesystemIO::DirListIfHasExtension(directory.c_str(), ".conf", false);

	for (const std::string& file : files) {
		if (mWeaponCount >= scMaxWeapons) {
			LogWarning(LC_SCRIPT, "Too many weapons, ignoring '{}'", file);
			break;
		}

		Weapon weapon;
		if (!LoadWeapon((std::string(scWeaponDirectory) + "/" + file).c_str(), weapon)) {
			continue;
		}

		uint32 insert_at = mWeaponCount;
		while (insert_at > 0 && mWeapons[insert_at - 1].Slot > weapon.Slot) {
			mWeapons[insert_at] = std::move(mWeapons[insert_at - 1]);
			--insert_at;
		}

		mWeapons[insert_at] = std::move(weapon);
		++mWeaponCount;
	}

	for (uint32 i = 0; i < mWeaponCount; i++) {
		Weapon& weapon = mWeapons[i];

		weapon.pScript = gScriptManager->LoadScript(weapon.ScriptPath);
		BindFunctions(weapon);

		if (weapon.pFnInit != nullptr) {
			weapon.pScript->CallFunctionPtr<void(const ScriptDef*, ScriptState*)>(weapon.pFnInit, &weapon.Def,
																				  &weapon.State);
		}

		LogInfo(LC_SCRIPT, "Loaded weapon '{}' (slot {}, {}/{} rounds)", weapon.Name, weapon.Slot,
				weapon.State.Magazine, weapon.State.Reserve);
	}

	if (mWeaponCount == 0) {
		return;
	}

	Weapon& first = mWeapons[0];
	ApplyAnimations(first);

	if (first.pFnEquip != nullptr) {
		first.pScript->CallFunctionPtr<void(const ScriptDef*, ScriptState*)>(first.pFnEquip, &first.Def, &first.State);
	}
}

void WeaponSystem::OnScriptsReloaded()
{
	for (uint32 i = 0; i < mWeaponCount; i++) {
		BindFunctions(mWeapons[i]);
	}
}

void WeaponSystem::ApplyAnimations(const Weapon& weapon)
{
	mpPlayer->SetViewModelAnimations(weapon.IdleAnim.CStr(), weapon.FireAnim.CStr(), weapon.ReloadAnim.CStr());
	mpPlayer->SetRecoilRecovery(weapon.Def.RecoilRecovery);
}

void WeaponSystem::SelectWeapon(uint32 index)
{
	if (index >= mWeaponCount) {
		return;
	}

	if (mbSwitching) {
		mPending = index;
		return;
	}

	if (index == mActive) {
		return;
	}

	mPending = index;
	mbSwitching = true;

	Weapon& active = mWeapons[mActive];

	if (active.pFnHolster != nullptr) {
		active.pScript->CallFunctionPtr<void(const ScriptDef*, ScriptState*)>(active.pFnHolster, &active.Def,
																			  &active.State);
	}
	else {
		active.State.Phase = ePhase::Lowered;
	}
}

void WeaponSystem::SwitchToNextWeapon()
{
	if (mWeaponCount < 2) {
		return;
	}

	const uint32 base = mbSwitching ? mPending : mActive;
	SelectWeapon((base + 1) % mWeaponCount);
}

void WeaponSystem::UpdateSwitching()
{
	if (!mbSwitching || mWeapons[mActive].State.Phase != ePhase::Lowered) {
		return;
	}

	mActive = mPending;
	mbSwitching = false;

	Weapon& next = mWeapons[mActive];
	ApplyAnimations(next);

	if (next.pFnEquip != nullptr) {
		next.pScript->CallFunctionPtr<void(const ScriptDef*, ScriptState*)>(next.pFnEquip, &next.Def, &next.State);
	}
	else {
		next.State.Phase = ePhase::Ready;
	}
}

void WeaponSystem::UpdateHolsterAmount()
{
	const ScriptState& state = mWeapons[mActive].State;
	const float32 progress = std::clamp(state.Timer / state.TimerTotal, 0.0f, 1.0f);

	float32 amount = 0.0f;

	switch (state.Phase) {
	case ePhase::Lowering:
		amount = 1.0f - progress;
		break;
	case ePhase::Lowered:
		amount = 1.0f;
		break;
	case ePhase::Raising:
		amount = progress;
		break;
	default:
		break;
	}

	mpPlayer->SetViewModelHolster(amount * amount * (3.0f - 2.0f * amount));
}

void WeaponSystem::Update(float32 delta_time)
{
	if (mWeaponCount == 0 || mpPlayer == nullptr) {
		return;
	}

	if (!mbEnabled) {
		mbFireLatched = false;
		return;
	}

	if (HasFlag(mScriptInput.InputFlags, eWeaponInputFlags::FirePressed)) {
		mbFireLatched = true;
	}
	if (!HasFlag(mScriptInput.InputFlags, eWeaponInputFlags::FireHeld)) {
		mbFireLatched = false;
	}

	if (HasFlag(mScriptInput.InputFlags, eWeaponInputFlags::SwitchWeapon)) {
		SwitchToNextWeapon();
	}

	Weapon& weapon = mWeapons[mActive];

	if (weapon.pFnUpdate != nullptr) {
		const JPH::Vec3 velocity = mpPlayer->Physics.pPlayerVirt->GetLinearVelocity();
		const float32 horizontal_speed = std::sqrt(velocity.GetX() * velocity.GetX() +
												   velocity.GetZ() * velocity.GetZ());

		if ((mScriptInput.InputFlags & eWeaponInputFlags::FireHeld) != 0 && mbFireLatched) {
			SetFlag(mScriptInput.InputFlags, eWeaponInputFlags::FireHeld);
		}
		else {
			ClearFlag(mScriptInput.InputFlags, eWeaponInputFlags::FireHeld);
		}

		if (!mpPlayer->Physics.bIsGrounded && !mpPlayer->IsFlyMode()) {
			mScriptInput.InputFlags |= eWeaponInputFlags::Airborne;
		}

		mScriptInput.MoveAmount = std::clamp(horizontal_speed / scSprintSpeed, 0.0f, 1.0f);

		weapon.pScript->CallFunctionPtr<void(const ScriptDef*, ScriptState*, const ScriptInput*, float32)>(
			weapon.pFnUpdate, &weapon.Def, &weapon.State, &mScriptInput, delta_time);
	}

	UpdateSwitching();
	UpdateHolsterAmount();
}

void WeaponSystem::HandleEvent(eEvent event)
{
	if (mpPlayer == nullptr || mWeaponCount == 0) {
		return;
	}

	const Weapon& weapon = mWeapons[mActive];

	switch (event) {
	case eEvent::Fire:
		mpPlayer->DoFireAnimation(weapon.ViewKickback);
		break;
	case eEvent::ReloadBegin:
		mpPlayer->DoReloadAnimation();
		break;
	default:
		break;
	}

	if (gCVars->Get("b_weapon_debug", false)) {
		LogInfo(LC_SCRIPT, "[{}] {} ({}/{}, {})", weapon.Name, EventName(event), weapon.State.Magazine,
				weapon.State.Reserve, ModeName(weapon.State.FireMode));
	}
}

float32 WeaponSystem::Trace(float32 yaw, float32 pitch, float32 range)
{
	mLastHit = physics::RayResult {};

	Ref<PerspectiveCamera>& camera = mpPlayer->pCamera;

	const Vec3f forward = camera->GetForwardVector();
	const Vec3f right = camera->GetRightVector();
	const Vec3f up = camera->GetUpVector();

	mLastDirection = (forward + right * std::tan(yaw) + up * std::tan(pitch)).Normalize();

	const physics::RayResult hit = gPhysics->pBackend->Raycast(camera->Position, mLastDirection * range);

	if (!hit.bHit) {
		return -1.0f;
	}

	mLastHit = hit;

	return (hit.Point - camera->Position).Length();
}

void WeaponSystem::ApplyHit(float32 damage, float32 force, bool decal)
{
	if (!mLastHit.bHit) {
		return;
	}

	JPH::BodyInterface& bodies = gPhysics->pBackend->GetBodyInterface();
	const JPH::EMotionType motion = bodies.GetMotionType(mLastHit.Body);

	const bool is_static = (motion == JPH::EMotionType::Static);

	physics::Body* hit_body = gPhysics->FindBody(mLastHit.Body);
	const Object* hit_object = (hit_body != nullptr) ? gObjectManager->GetObject(hit_body->GetObjectID()) : nullptr;
	const bool bleeds = (hit_object != nullptr) && hit_object->Bleeds();

	if (bleeds) {
		if (decal) {
			SpawnBloodSplatter(is_static);
		}
	}
	else if (is_static) {
		if (decal) {
			gDecalManager->AddBulletHole(mLastHit.Point, mLastHit.Normal);
		}
	}

	if (!is_static && force > 0.0f) {
		bodies.ActivateBody(mLastHit.Body);
		bodies.AddImpulse(mLastHit.Body,
						  JPH::Vec3(mLastDirection.X * force, mLastDirection.Y * force, mLastDirection.Z * force));
	}

	if (gCVars->Get("b_weapon_debug", false)) {
		LogInfo(LC_SCRIPT, "hit for {:.1f} damage at {}", damage, mLastHit.Point);
	}
}

static float32 RandomUnit() { return static_cast<float32>(FastRand32() >> 8) * (1.0f / 16777216.0f); }

void WeaponSystem::SpawnBloodSplatter(bool is_static)
{
	constexpr float32 exit_range = 3.0f;
	constexpr float32 floor_range = 2.0f;

	const Vec3f point = mLastHit.Point;

	if (is_static) {
		gDecalManager->AddBloodSplat(point, mLastHit.Normal, 0.5f);
	}

	const Vec3f scatter(RandomUnit() - 0.5f, RandomUnit() - 0.5f, RandomUnit() - 0.5f);
	const Vec3f exit_direction = (mLastDirection + (scatter * 0.2f)).Normalize();

	const physics::RayResult exit_hit = gPhysics->pBackend->Raycast(point, exit_direction * exit_range, mLastHit.Body);

	if (exit_hit.bHit) {
		const float32 distance_fade = 1.0f - (0.6f * ((exit_hit.Point - point).Length() / exit_range));
		gDecalManager->AddBloodSplat(exit_hit.Point, exit_hit.Normal, 0.9f * distance_fade);
	}

	const Vec3f floor_origin = point + (mLastHit.Normal * 0.05f);
	const physics::RayResult floor_hit = gPhysics->pBackend->Raycast(floor_origin, -Vec3f::sUp * floor_range,
																	 mLastHit.Body);

	if (floor_hit.bHit) {
		const float32 distance_fade = 1.0f - (0.5f * ((floor_hit.Point - floor_origin).Length() / floor_range));
		gDecalManager->AddBloodSplat(floor_hit.Point, floor_hit.Normal, 0.6f * distance_fade);
	}
}

void WeaponSystem::Kick(float32 pitch, float32 yaw) { mpPlayer->AddRecoil(pitch, yaw); }

void WeaponSystem::RenderHud()
{
	if (!mbEnabled || mWeaponCount == 0 || gTextRenderer == nullptr) {
		return;
	}

	const Weapon& weapon = mWeapons[mActive];
	const ScriptState& state = weapon.State;

	const Vec2u window_size = gGraphics->GetWindow()->GetSize();
	const float32 right_edge = static_cast<float32>(window_size.X) - 32.0f;
	const float32 bottom_edge = 64.0f;

	const uint32 white = PackColor(255, 255, 255);
	const uint32 amber = PackColor(255, 190, 40);
	const uint32 red = PackColor(255, 70, 50);
	const uint32 grey = PackColor(170, 170, 170);

	const float32 ammo_scale = 1.0f;
	const float32 label_scale = 1.0f;

	const String ammo_text = String::Fmt("{:02} / {:03}", state.Magazine, state.Reserve);
	const String mode_text = String::Fmt("{} [{}]", weapon.Name.CStr(), ModeName(state.FireMode));

	const float32 ammo_width = static_cast<float32>(ammo_text.GetLength() * TextRenderer::scGlyphWidth);
	const float32 mode_width = static_cast<float32>(mode_text.GetLength() * TextRenderer::scGlyphWidth);

	const float32 ammo_height = static_cast<float32>(TextRenderer::scGlyphHeight);
	const float32 label_height = static_cast<float32>(TextRenderer::scGlyphHeight);

	uint32 ammo_color = white;
	if (state.Magazine == 0) {
		ammo_color = red;
	}
	else if (state.Magazine * 4 <= weapon.Def.MagazineSize) {
		ammo_color = amber;
	}

	const float32 ammo_y = bottom_edge - ammo_height;
	gTextRenderer->DrawTextAt(ammo_text.CStr(), Vec2f(right_edge - ammo_width, ammo_y), 1.0, ammo_color);

	const float32 mode_y = ammo_y - label_height - 6.0f;
	gTextRenderer->DrawTextAt(mode_text.CStr(), Vec2f(right_edge - mode_width, mode_y), 1.0, grey);

	const char* status = nullptr;

	if (state.Phase == ePhase::Reloading) {
		status = "reloading";
	}
	else if (state.Magazine == 0 && state.Reserve == 0) {
		status = "no ammo";
	}
	else if (state.Magazine == 0) {
		status = "reload";
	}

	if (status != nullptr) {
		const float32 status_width = static_cast<float32>(std::strlen(status) * TextRenderer::scGlyphWidth) * 1.0;
		gTextRenderer->DrawTextAt(status, Vec2f(right_edge - status_width, mode_y - label_height - 6.0f), 1.0, amber);
	}
}

} // namespace fx::weapon
