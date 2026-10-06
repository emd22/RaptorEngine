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
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>

namespace fx::weapon {

using namespace renderer;

static constexpr const char* scWeaponDirectory = "RaptorData/Data/Weapons";
static constexpr float32 scSprintSpeed = 5.0f;

namespace {

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

uint32 PackColor(uint8 r, uint8 g, uint8 b) { return Color::FromRGBA(r, g, b, 255).AsUInt(); }

} // namespace

bool WeaponSystem::LoadWeapon(const char* path, Weapon& weapon)
{
	std::vector<uint8> bytes;

	if (!ReadConfigFileBytes(path, bytes)) {
		LogError(LC_SCRIPT, "Could not read weapon config '{}'", path);
		return false;
	}

	const RxHost host = MakeConfigHost();
	const std::string constants_path = GetConfigConstantsPath();

	int32 error = 0;

	RxWeaponDefOwner* owner = rx_weapon_def_parse(bytes.data(), bytes.size(), constants_path.c_str(), &host, &error);

	if (owner == nullptr) {
		if (error == RX_WEAPON_ERROR_NO_SCRIPT) {
			LogError(LC_SCRIPT, "Weapon config '{}' has no Script entry", path);
		}
		else {
			LogError(LC_SCRIPT, "Could not parse weapon config '{}'", path);
		}
		return false;
	}

	const RxWeaponDef& loaded = *rx_weapon_def_get(owner);

	weapon.ScriptPath = String(loaded.script);
	weapon.Name = String(loaded.name);
	weapon.Slot = loaded.slot;

	weapon.IdleAnim = String(loaded.idle_anim);
	weapon.FireAnim = String(loaded.fire_anim);
	weapon.ReloadAnim = String(loaded.reload_anim);

	weapon.ViewKickDegrees = loaded.view_kick;

	static_assert(sizeof(ScriptDef) == sizeof(RxWeaponScriptDef));
	std::memcpy(&weapon.Def, &loaded.def, sizeof(ScriptDef));

	rx_weapon_def_free(owner);

	const ScriptDef& def = weapon.Def;

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
		const Vec3f velocity = mpPlayer->Physics.GetLinearVelocity();
		const float32 horizontal_speed = std::sqrt(velocity.X * velocity.X + velocity.Z * velocity.Z);

		if ((mScriptInput.InputFlags & eWeaponInputFlags::FireHeld) != 0 && mbFireLatched) {
			SetFlag(mScriptInput.InputFlags, eWeaponInputFlags::FireHeld);
		}
		else {
			ClearFlag(mScriptInput.InputFlags, eWeaponInputFlags::FireHeld);
		}

		if (!mpPlayer->Physics.IsGrounded() && !mpPlayer->IsFlyMode()) {
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
		mpPlayer->DoFireAnimation(weapon.ViewKickDegrees);
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

	RxPhysicsWorld* world = gPhysics->pBackend->pWorld;
	const uint32 hit_body_id = mLastHit.Body.Id;

	const bool is_static = (rx_physics_is_dynamic(world, hit_body_id) == 0);

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
		const float32 impulse[3] = { mLastDirection.X * force, mLastDirection.Y * force, mLastDirection.Z * force };
		rx_physics_push(world, hit_body_id, impulse);
	}

	if (gCVars->Get("b_weapon_debug", false)) {
		LogInfo(LC_SCRIPT, "hit for {:.1f} damage at {}", damage, mLastHit.Point);
	}
}

void WeaponSystem::SpawnBloodSplatter(bool is_static)
{
	constexpr float32 exit_range = 3.0f;
	constexpr float32 floor_range = 2.0f;

	const Vec3f& point = mLastHit.Point;

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
