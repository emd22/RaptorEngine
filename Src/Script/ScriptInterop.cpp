#include "ScriptInterop.hpp"

#include <Asset/MeshGen.hpp>
#include <Audio/AudioSystem.hpp>
#include <Blockout.hpp>
#include <CVar.hpp>
#include <Controls.hpp>
#include <Core/Random.hpp>
#include <Editor/EditOperation.hpp>
#include <Editor/EditorTool.hpp>
#include <Editor/Operation/ObjectOperation.hpp>
#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>
#include <Math/SIMDHelper.hpp>
#include <NPC.hpp>
#include <Object/ObjectID.hpp>
#include <Object/ObjectManager.hpp>
#include <Particle/ParticleSystem.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/LightManager.hpp>
#include <Renderer/ReflectionProbe.hpp>
#include <Script/ObjectScripts.hpp>
#include <Script/TimerManager.hpp>
#include <Weapon/WeaponSystem.hpp>
#include <World.hpp>
#include <WorldGrid.hpp>
#include <cmath>
#include <vector>


namespace fx::script {

static Object* N_RObjectGet(uint32 id)
{
	ObjectID obj_id(id);
	if (obj_id.IsInvalid()) {
		return nullptr;
	}

	return gObjectManager->GetObject(obj_id);
}

static Object* N_RObjectFind(const char* name)
{
	if (name == nullptr) {
		return nullptr;
	}

	return gObjectManager->FindObject(HashStr32(name));
}

/////////////////////////////////////
// Edit operations
/////////////////////////////////////

enum class eScriptOpType
{
	Move,
	Scale,
	Dupe,
	Create,
	Delete,
	Rotate,
};

static FVec3 N_editor_push_op_vec3(Object* obj, int op_type, FLOAT4 original, FLOAT4 updated, int32 group_size)
{
#ifdef FX_IS_EDITOR
	if (obj == nullptr) {
		return FVec3::sZero;
	}

	const FVec3 before(original);
	const FVec3 after(updated);

	switch (static_cast<eScriptOpType>(op_type)) {
	case eScriptOpType::Move:
		gEditor->EmplaceEditOperation<editor::MoveOperation>(obj, before, after, group_size);
		break;
	case eScriptOpType::Rotate:
		gEditor->EmplaceEditOperation<editor::RotateOperation>(obj, before, after, group_size);
		break;
	case eScriptOpType::Scale:
		gEditor->EmplaceEditOperation<editor::ScaleFaceOperation>(obj, before, after.X, group_size);
		break;
	default:
		LogWarning("Editor op type {} cannot be pushed with a vector", op_type);
		return FVec3::sZero;
	}

	return after;
#else
	return FVec3::sZero;
#endif
}

static Object* N_editor_op_create_object(FLOAT4 position, int32 group_size)
{
#ifdef FX_IS_EDITOR
	return gEditor
		->EmplaceEditOperation<editor::CreateOperation>(FVec3(position), group_size, gEditor->GetNewBrushMaterial())
		.GetCreated();
#else
	return nullptr;
#endif
}

static Object* N_editor_op_dupe_object(Object* object_to_dupe, FLOAT4 position, int32 group_size)
{
#ifdef FX_IS_EDITOR
	if (object_to_dupe == nullptr) {
		return nullptr;
	}

	return gEditor
		->EmplaceEditOperation<editor::DupeOperation>(object_to_dupe, FVec3(position), group_size,
													  gEditor->AllocateDupeSubview(object_to_dupe))
		.GetDupe();
#else
	return nullptr;
#endif
}

static int32 N_editor_snap_selection_to_grid()
{
#ifdef FX_IS_EDITOR
	return static_cast<int32>(gEditor->SnapSelectionToGrid());
#else
	return 0;
#endif
}

static void N_editor_push_op_delete(Object* obj, int32 group_size)
{
#ifdef FX_IS_EDITOR
	if (gEditor->IsSimulationMode()) {
		return;
	}

	gEditor->DeleteObject(obj, group_size);
#endif
}


static void N_RObject_MoveTo(Object* obj, FLOAT4 position)
{
	if (obj == nullptr) {
		return;
	}


	obj->SetPosition(FVec3(position));
}

static void N_RObject_MoveBy(Object* obj, FLOAT4 by)
{
	if (obj == nullptr) {
		return;
	}

	obj->MoveBy(FVec3(by));
}


static FLOAT4 N_RObject_GetPosition(Object* obj)
{
	if (obj == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return obj->GetPosition().mIntrin;
}

static void N_RObject_RotateToEuler(Object* obj, FLOAT4 euler_radians)
{
	if (obj == nullptr) {
		return;
	}

	obj->SetRotation(FQuat::FromEulerAngles(FVec3(euler_radians)));
}

static FLOAT4 N_RObject_GetRotationEuler(Object* obj)
{
	if (obj == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return obj->mRotation.GetEulerAngles().mIntrin;
}

static void N_print_float4(FLOAT4 v) { LogInfo(LC_SCRIPT, "{}", FVec4(v)); }

static uint32 N_RObject_GetTags(Object* obj)
{
	if (obj == nullptr) {
		return 0;
	}

	return static_cast<uint32>(obj->Tags);
}

static void N_RObject_SetTags(Object* obj, uint32 tags)
{
	if (obj == nullptr) {
		return;
	}

	obj->SetTags(static_cast<eObjectTag>(tags));
}


static float32 N_object_direction_scale(Object* obj, FLOAT4 direction)
{
	if (obj == nullptr) {
		return 0.0f;
	}

	// A brush's reach along a face normal is exactly where that face is, which its bounds only give for boxes
	const Brush* brush = gWorld->pPrototype->GetBrush(obj);
	if (brush != nullptr) {
		return brush->GetSupport(FVec3(direction).Normalize());
	}

	return obj->GetDirectionScale(FVec3(direction));
}

static FLOAT4 N_object_get_enter_direction(Object* obj)
{
	if (obj == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return gObjectScripts->GetEnterDirection(obj->ID).mIntrin;
}

static FLOAT4 N_object_get_exit_direction(Object* obj)
{
	if (obj == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return gObjectScripts->GetExitDirection(obj->ID).mIntrin;
}

static FLOAT4 N_object_local_to_world(Object* obj, FLOAT4 point)
{
	if (obj == nullptr) {
		return point;
	}

	const FVec3 p(point);
	const FVec4 world = obj->GetWorldMatrix() * FVec4(p.X, p.Y, p.Z, 1.0f);

	return FVec3(world.X, world.Y, world.Z).mIntrin;
}

static FLOAT4 N_object_local_dir_to_world(Object* obj, FLOAT4 direction)
{
	if (obj == nullptr) {
		return direction;
	}

	const FVec3 d(direction);
	const FVec4 world = obj->GetWorldMatrix() * FVec4(d.X, d.Y, d.Z, 0.0f);
	const FVec3 world_dir(world.X, world.Y, world.Z);

	if (world_dir.IsCloseTo(simd::LoadFloat4(0.0f))) {
		return simd::LoadFloat4(0.0f);
	}

	return world_dir.Normalize().mIntrin;
}

static FLOAT4 N_object_ray_get_face(Object* obj)
{
	if (obj == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	if (obj->IsProbeVolume()) {
		FVec3 face;

		const float32 distance = obj->RaycastBounds(gWorld->Player.pCamera->GetPosition(),
													gWorld->Player.pCamera->GetForwardVector(), face);

		return (distance >= 0.0f) ? face.mIntrin : simd::LoadFloat4(0.0f);
	}

	if (gWorld->pPrototype->GetBrush(obj) != nullptr) {
		FVec3 face_normal;

		if (!gWorld->pPrototype->RaycastFace(obj, gWorld->Player.pCamera->GetPosition(),
											 gWorld->Player.pCamera->GetForwardVector(), face_normal)) {
			return simd::LoadFloat4(0.0f);
		}

		return face_normal.mIntrin;
	}

	physics::Body* body = gPhysics->GetBody(obj->PhysicsID);
	if (body == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return gPhysics->pBackend->RaycastGetFaceOfBox(body->GetBody(), gWorld->Player.pCamera->GetPosition(),
												   gWorld->Player.pCamera->GetForwardVector() * 4.0f);
}

static void N_object__select_object_internal(Object* obj, bool is_selected, bool append_selection)
{
#ifdef FX_IS_EDITOR
	if (gEditor->IsSimulationMode()) {
		return;
	}

	// Deselect object
	if (!is_selected || obj == nullptr) {
		gEditor->ClearSelection();
		return;
	}

	// Select an object
	gEditor->SelectObject(obj, append_selection);
#endif
}


static FLOAT4 N_camera_position() { return gWorld->GetCurrentCamera()->GetPosition().mIntrin; }
static FLOAT4 N_player_get_position(void*) { return gWorld->Player.Position.mIntrin; }

static void N_player_set_speed_multiplier(void*, float mult) { gWorld->Player.SpeedMultiplier = mult; }
static bool N_player_is_flymode(void*) { return gWorld->Player.IsFlyMode(); }

static FLOAT4 N_player_ray_get_point(void*, float32 range)
{
	physics::RayResult rr = gPhysics->pBackend->Raycast(gWorld->Player.pCamera->GetPosition(),
														gWorld->Player.pCamera->GetForwardVector() * range);

	if (!rr.bHit) {
		return simd::LoadFloat4(0.0f);
	}

	return rr.Point.mIntrin;
}

static FLOAT4 N_player_ray_get_normal(void*, float32 range)
{
	physics::RayResult rr = gPhysics->pBackend->Raycast(gWorld->Player.pCamera->GetPosition(),
														gWorld->Player.pCamera->GetForwardVector() * range);

	if (!rr.bHit) {
		return simd::LoadFloat4(0.0f);
	}

	return rr.Normal.mIntrin;
}

/// Where the crosshair's ray meets the plane through `point` facing `normal`, or `point` if the ray never reaches it
static FLOAT4 N_camera_ray_to_plane(FLOAT4 point, FLOAT4 normal)
{
	const FVec3 origin = gWorld->Player.pCamera->GetPosition();
	const FVec3 direction = gWorld->Player.pCamera->GetForwardVector();
	const FVec3 plane_normal(normal);

	const float32 facing = plane_normal.Dot(direction);
	if (std::abs(facing) < 1e-5f) {
		return point;
	}

	const float32 distance = plane_normal.Dot(FVec3(point) - origin) / facing;
	if (distance < 0.0f) {
		return point;
	}

	return (origin + direction * distance).mIntrin;
}

static FLOAT4 N_float4_round(FLOAT4 value) { return simd::Round(value); }
static FLOAT4 N_float4_floor(FLOAT4 value) { return simd::Floor(value); }
static FLOAT4 N_float3_abs(FLOAT4 value)
{
#ifdef FX_USE_NEON
	return Neon::SetSigns<1>(value);
#elif FX_USE_AVX
	return SSE::SetSigns<1>(value);
#endif
}


static bool N_RKey_IsUp(uint32 key) { return ControlManager::IsKeyUp(static_cast<eKey>(key)); }
static bool N_RKey_IsDown(uint32 key) { return ControlManager::IsKeyDown(static_cast<eKey>(key)); }
static bool N_RKey_IsPressed(uint32 key) { return ControlManager::IsKeyPressed(static_cast<eKey>(key)); }

static float N_float_sign(float value) { return MathUtil::GetSign(value); }
static float N_float_sqrt(float value) { return std::sqrt(value); }

static void N_blockout_reload_object(Object* object) { gWorld->pPrototype->RebuildObject(object); }

static void N_blockout_object_scale(Object* object, FLOAT4 face_dir, FLOAT4 magnitude)
{
	gWorld->pPrototype->MoveFace(object, FVec3(face_dir), FVec3(magnitude).X);
}

/// The centre of the face facing along `face`, in the object's local space
static FLOAT4 N_blockout_face_center(Object* object, FLOAT4 face)
{
	const Brush* brush = gWorld->pPrototype->GetBrush(object);
	if (brush == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	const int32 plane_index = brush->FindPlane(FVec3(face));
	if (plane_index == Brush::scNoPlane) {
		return simd::LoadFloat4(0.0f);
	}

	return brush->GetFaceCenter(static_cast<uint32>(plane_index)).mIntrin;
}

/// Where the crosshair's ray hits a blockout, in world space
static FLOAT4 N_blockout_ray_hit_point(Object* object)
{
	FVec3 face_normal;
	FVec3 point;

	if (object == nullptr ||
		!gWorld->pPrototype->RaycastFace(object, gWorld->Player.pCamera->GetPosition(),
										 gWorld->Player.pCamera->GetForwardVector(), face_normal, &point)) {
		return simd::LoadFloat4(0.0f);
	}

	return point.mIntrin;
}

static void N_blockout_preview_box(FLOAT4 min, FLOAT4 max)
{
	FVec3 position;
	const Brush brush = gWorld->pPrototype->MakeWorldBox(FVec3(min), FVec3(max), position);

	gWorld->pPrototype->ShowPreview(position, FQuat::scIdentity, brush);
}

/// Previews the piece that a clip would split off
static void N_blockout_preview_clip(Object* object, FLOAT4 point_a, FLOAT4 point_b, FLOAT4 face_normal)
{
	Brush::PlaneList kept;
	Brush::PlaneList split;
	FVec3 split_position;

	if (object == nullptr || !gWorld->pPrototype->GetClipPieces(object, FVec3(point_a), FVec3(point_b),
																FVec3(face_normal), kept, split, split_position)) {
		gWorld->pPrototype->HidePreview();
		return;
	}

	gWorld->pPrototype->ShowPreview(split_position, object->mRotation, Brush::FromPlanes(split));
}

static void N_blockout_hide_preview() { gWorld->pPrototype->HidePreview(); }

static Object* N_blockout_create_box(FLOAT4 min, FLOAT4 max)
{
#ifdef FX_IS_EDITOR
	return gEditor->CreateWorldBox(FVec3(min), FVec3(max));
#else
	return nullptr;
#endif
}

/// Splits a blockout in two along a line drawn on one of its faces. Both pieces are kept.
static bool N_blockout_clip(Object* object, FLOAT4 point_a, FLOAT4 point_b, FLOAT4 face_normal)
{
#ifdef FX_IS_EDITOR
	return gEditor->ClipObject(object, FVec3(point_a), FVec3(point_b), FVec3(face_normal));
#else
	return false;
#endif
}

static int32 N_blockout_subtract(bool keep_cutters)
{
#ifdef FX_IS_EDITOR
	return static_cast<int32>(gEditor->SubtractSelection(keep_cutters));
#else
	return 0;
#endif
}

static void N_blockout_edit_face_texture(Object* object, FLOAT4 face, uint32 edit, FLOAT4 amount)
{
#ifdef FX_IS_EDITOR
	if (object == nullptr || gEditor->IsSimulationMode() || !gEditor->CanEditObject(object)) {
		return;
	}

	const Brush* brush = gWorld->pPrototype->GetBrush(object);
	if (brush == nullptr) {
		return;
	}

	const FVec3 amount_vec(amount);

	Brush::PlaneList planes_after;

	if (!gWorld->pPrototype->GetFaceTextureEdit(object, FVec3(face), static_cast<eFaceTextureEdit>(edit),
												FVec2(amount_vec.X, amount_vec.Y), planes_after)) {
		return;
	}

	gEditor->EmplaceEditOperation<editor::BrushEditOperation>(object, brush->Planes, planes_after);
#endif
}

static Object* N_blockout_new_object(FLOAT4 position) { return gWorld->pPrototype->NewObject(FVec3(position)); }
static Object* N_blockout_dupe_object(Object* object) { return gWorld->pPrototype->DupeObject(object); }
static void N_blockout_destroy_object(Object* object) { gWorld->pPrototype->DestroyObject(object); }


/////////////////////////////////////
// Reflection probes
/////////////////////////////////////

static void N_probe_volume_mark(Object* obj, bool enabled)
{
	if (obj == nullptr) {
		return;
	}

	obj->ClearTag(eObjectTag::ReflectionProbe);
	obj->SetProbeVolume(enabled);
}

static void N_reflection_probe_mark(Object* obj, bool enabled)
{
	if (obj == nullptr) {
		return;
	}

	obj->SetReflectionProbe(enabled);
}

static bool N_reflection_probe_is_marked(Object* obj) { return obj != nullptr && obj->IsReflectionProbe(); }

static int32 N_reflection_probe_rebuild()
{
	return static_cast<int32>(gReflectionProbes->RebuildReflectionProbesFromWorld());
}

static void N_reflection_probe_bake()
{
	gReflectionProbes->RebuildReflectionProbesFromWorld();
	gReflectionProbes->BeginBake();
}

static bool N_probe_volume_is_marked(Object* obj) { return obj != nullptr && obj->IsProbeVolume(); }

static bool N_probe_is_baking() { return gReflectionProbes->IsBaking(); }

static bool N_probe_save() { return gReflectionProbes->SaveProbes(); }

static void N_cvar_set_int(const char* name, int64 value) { gCVars->Set(name, value); }
static void N_cvar_set_float(const char* name, float32 value) { gCVars->Set(name, value); }
static void N_cvar_set_string(const char* name, const char* value) { gCVars->Set(name, value); }

static int64 N_cvar_get_int(const char* name, int64 fallback) { return gCVars->Get(name, fallback); }
static float32 N_cvar_get_float(const char* name, float32 fallback) { return gCVars->Get(name, fallback); }

static uint32 N_fastrand32() { return FastRand32(); }

static float32 N_random_range(float32 lo, float32 hi)
{
	const float32 unit = static_cast<float32>(FastRand32() >> 8) * (1.0f / 16777216.0f);
	return lo + (hi - lo) * unit;
}

static int64 N_particle_play(const char* name, FLOAT4 position, FLOAT4 direction)
{
	return gParticleSystem->Play(name, FVec3(position), FVec3(direction)).Pack();
}

static void N_particle_move(int64 effect, FLOAT4 position, FLOAT4 direction)
{
	gParticleSystem->SetTransform(ParticleEffectID::Unpack(effect), FVec3(position), FVec3(direction));
}

static void N_particle_stop(int64 effect) { gParticleSystem->Stop(ParticleEffectID::Unpack(effect)); }
static void N_particle_kill(int64 effect) { gParticleSystem->Kill(ParticleEffectID::Unpack(effect)); }
static bool N_particle_alive(int64 effect) { return gParticleSystem->IsAlive(ParticleEffectID::Unpack(effect)); }

static int64 N_sound_play(const char* name, FLOAT4 position) { return gAudio->Play(name, FVec3(position)).Pack(); }
static int64 N_sound_play_2d(const char* name) { return gAudio->Play2D(name).Pack(); }

static void N_sound_move(int64 sound, FLOAT4 position) { gAudio->SetPosition(SoundID::Unpack(sound), FVec3(position)); }

static void N_sound_set_volume(int64 sound, float32 volume) { gAudio->SetVolume(SoundID::Unpack(sound), volume); }
static void N_sound_stop(int64 sound) { gAudio->Stop(SoundID::Unpack(sound)); }
static void N_sound_kill(int64 sound) { gAudio->Kill(SoundID::Unpack(sound)); }
static bool N_sound_alive(int64 sound) { return gAudio->IsPlaying(SoundID::Unpack(sound)); }
static void N_sound_snapshot_start(const char* name) { gAudio->StartSnapshot(name); }
static void N_sound_snapshot_stop(const char* name) { gAudio->StopSnapshot(name); }

static int64 N_timer_after(float32 seconds, const char* fn)
{
	return gTimers->StartScript(GetCallingScript(), seconds, 1, fn).Pack();
}

static int64 N_timer_every(float32 seconds, const char* fn)
{
	return gTimers->StartScript(GetCallingScript(), seconds, -1, fn).Pack();
}

static int64 N_timer_repeat(float32 seconds, int32 count, const char* fn)
{
	return gTimers->StartScript(GetCallingScript(), seconds, count, fn).Pack();
}

static void N_timer_cancel(int64 timer) { gTimers->Cancel(TimerID::Unpack(timer)); }
static void N_timer_cancel_all() { gTimers->CancelOwnedBy(GetCallingScript()); }
static bool N_timer_active(int64 timer) { return gTimers->IsActive(TimerID::Unpack(timer)); }
static float32 N_timer_remaining(int64 timer)
{
	return static_cast<float32>(gTimers->GetRemaining(TimerID::Unpack(timer)));
}
static float32 N_timer_clock() { return static_cast<float32>(gTimers->GetTime()); }
static void N_timer_list() { gTimers->Dump(); }

static bool N_portal_set_open(const char* name, bool open)
{
	AreaGraph& graph = gWorldGrid->GetAreaGraph();
	const int32 portal = graph.FindPortal(name);

	if (portal < 0) {
		return false;
	}

	graph.SetPortalOpen(static_cast<uint32>(portal), open);

	return true;
}

static bool N_portal_is_open(const char* name)
{
	const AreaGraph& graph = gWorldGrid->GetAreaGraph();
	const int32 portal = graph.FindPortal(name);

	if (portal < 0) {
		return true;
	}

	return graph.GetPortal(static_cast<uint32>(portal)).bOpen;
}

static float32 N_RWeapon_WeaponTrace(float32 yaw, float32 pitch, float32 range)
{
	return gWorld->Player.Weapons.Trace(yaw, pitch, range);
}

static void N_RWeapon_ApplyHit(float32 damage, float32 force, bool decal)
{
	gWorld->Player.Weapons.ApplyHit(damage, force, decal);
}

static void N_RWeapon_DoWeaponKick(float32 pitch, float32 yaw) { gWorld->Player.Weapons.Kick(pitch, yaw); }

static void N_RWeapon_HandleEvent(int32 event)
{
	gWorld->Player.Weapons.HandleEvent(static_cast<weapon::eEvent>(event));
}

static NPC* FindNPC(int32 npc)
{
	if (npc <= 0) {
		return nullptr;
	}

	return gNPCManager->FindByRagdollSerial(static_cast<uint32>(npc));
}

static int32 N_npc_find(const char* name)
{
	NPC* npc = gNPCManager->FindByName(name);

	if (npc == nullptr || !npc->GetRagdoll().IsCreated()) {
		return 0;
	}

	return static_cast<int32>(npc->GetRagdollSerial());
}

static int32 N_npc_last_hit() { return static_cast<int32>(gWorld->Player.Weapons.GetLastHitNPC()); }

static bool N_npc_is_alive(int32 npc)
{
	const NPC* found = FindNPC(npc);
	return found != nullptr && found->IsAlive();
}

static bool N_npc_behaviour_trigger(int32 npc, const char* behaviour)
{
	NPC* found = FindNPC(npc);
	return found != nullptr && found->TriggerBehaviour(behaviour);
}

static bool N_npc_behaviour_stop(int32 npc, const char* behaviour)
{
	NPC* found = FindNPC(npc);
	return found != nullptr && found->StopBehaviour(behaviour);
}

static bool N_npc_behaviour_active(int32 npc, const char* behaviour)
{
	NPC* found = FindNPC(npc);
	return found != nullptr && behaviour != nullptr && found->GetBehaviours().IsRunning(behaviour);
}

static bool N_npc_behaviour_set(int32 npc, const char* behaviour, const char* setting, float32 value)
{
	if (npc < 0) {
		return false;
	}

	return gNPCManager->SetBehaviourSetting(static_cast<uint32>(npc), behaviour, setting, value);
}

static float32 N_npc_behaviour_get(int32 npc, const char* behaviour, const char* setting)
{
	float32 value = 0.0f;

	if (npc < 0 || !gNPCManager->GetBehaviourSetting(static_cast<uint32>(npc), behaviour, setting, value)) {
		return 0.0f;
	}

	return value;
}

static int32 N_npc_count() { return static_cast<int32>(gNPCManager->GetCount()); }

static int32 N_npc_at(int32 index)
{
	if (index < 0 || static_cast<uint32>(index) >= gNPCManager->GetCount()) {
		return 0;
	}

	NPC* npc = gNPCManager->Get(static_cast<size_t>(index));

	if (!npc->GetRagdoll().IsCreated()) {
		return 0;
	}

	return static_cast<int32>(npc->GetRagdollSerial());
}

static bool N_npc_walk_to(int32 npc, FLOAT4 target)
{
	NPC* found = FindNPC(npc);

	if (found == nullptr || !found->IsAlive()) {
		return false;
	}

	found->GetBrain().WalkTo(FVec3(target));
	return true;
}

static bool N_npc_flee(int32 npc, FLOAT4 from)
{
	NPC* found = FindNPC(npc);

	if (found == nullptr || !found->IsAlive()) {
		return false;
	}

	found->GetBrain().Flee(FVec3(from));
	return true;
}

static bool N_npc_idle(int32 npc)
{
	NPC* found = FindNPC(npc);

	if (found == nullptr) {
		return false;
	}

	found->GetBrain().Idle();
	return true;
}

static int32 N_npc_brain_state(int32 npc)
{
	const NPC* found = FindNPC(npc);

	if (found == nullptr) {
		return 0;
	}

	return static_cast<int32>(found->GetBrain().GetState());
}

static void N_npc_stats() { gNPCManager->LogStats(); }

static bool N_npc_model_ready()
{
	gNPCManager->RequestModel();
	return gNPCManager->GetModel() != nullptr;
}

static int32 N_npc_spawn(FLOAT4 position, float32 yaw)
{
	static uint32 sSpawnCount = 0;

	const Object* model = gNPCManager->GetModel();

	if (model == nullptr) {
		return 0;
	}

	NPCDesc desc;
	desc.Name = String::Fmt("script_npc_{}", sSpawnCount);
	desc.Position = FVec3(position);
	desc.Yaw = yaw;
	desc.IdleAnimation = "walk";
	desc.WalkAnimation = "walk";

	++sSpawnCount;

	NPC* npc = gNPCManager->Spawn(model, desc);

	if (npc == nullptr || !npc->GetRagdoll().IsCreated()) {
		return 0;
	}

	return static_cast<int32>(npc->GetRagdollSerial());
}

static void N_npc_destroy(int32 npc)
{
	if (NPC* found = FindNPC(npc)) {
		gNPCManager->Destroy(found);
	}
}

/// The NPC's pelvis in world space, which unlike the root object follows the ragdoll
static FLOAT4 N_npc_position(int32 npc)
{
	NPC* found = FindNPC(npc);

	if (found == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return found->GetRagdoll().GetBodyOrigin(0).mIntrin;
}

static FLOAT4 N_npc_velocity(int32 npc)
{
	NPC* found = FindNPC(npc);

	if (found == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	const JPH::Vec3 velocity = gPhysics->pBackend->GetBodyInterface().GetLinearVelocity(
		found->GetRagdoll().GetBodyID(0));

	return FVec3(velocity.GetX(), velocity.GetY(), velocity.GetZ()).mIntrin;
}

static float32 N_npc_health(int32 npc)
{
	const NPC* found = FindNPC(npc);

	if (found == nullptr) {
		return 0.0f;
	}

	return found->GetHealth();
}

/// Must match NPC_STATE in Scripts/interop.strata
static int32 N_npc_state(int32 npc)
{
	NPC* found = FindNPC(npc);

	if (found == nullptr) {
		return 0;
	}

	if (found->IsRagdoll()) {
		return 5;
	}

	if (found->IsDying()) {
		return 4;
	}

	if (found->IsFallen()) {
		return 3;
	}

	if (found->GetRagdoll().IsBalancing()) {
		return 2;
	}

	return 1;
}

/////////////////////////////////////
// Physics props
/////////////////////////////////////

static int64 N_prop_spawn_box(const char* material, float32 size, float32 mass, float32 gravity_scale, FLOAT4 position,
							  FLOAT4 velocity)
{
	static uint32 sSpawnCount = 0;

	if (material == nullptr || size <= 0.0f || mass <= 0.0f || gWorld->pPrototype == nullptr) {
		return 0;
	}

	const String name = String::Fmt("script_prop_{}", sSpawnCount);
	const float32 half_size = size * 0.5f;

	++sSpawnCount;

	Object* object = gObjectManager->NewObject(name, gWorld->pPrototype->GetFaceMaterial(material), eObjectTag::None);

	object->pMesh = MeshGen::MakeCube(CubeGenOptions::Uniform(half_size))->AsDefaultMesh();
	object->Bounds.Min = FVec3(-half_size);
	object->Bounds.Max = FVec3(half_size);
	object->SetShadowCaster(true);
	object->SetPosition(FVec3(position));

	physics::BodyProps props;
	props.Density = mass / (size * size * size);

	physics::Body* body = gPhysics->NewBody(name);
	body->CreatePrimitiveBody(physics::ePrimitiveType::Box, FVec3(size), physics::eMotionType::Dynamic, props);
	body->Teleport(object->GetPosition(), object->mRotation);
	body->SetLinearVelocity(FVec3(velocity));

	gPhysics->pBackend->GetBodyInterface().SetGravityFactor(body->GetBodyID(), gravity_scale);

	object->AttachCollider(body);
	gWorld->AttachLoaded(object);

	return static_cast<int64>(object->Name.GetHash());
}

static Object* FindProp(int64 prop)
{
	if (prop == 0) {
		return nullptr;
	}

	return gObjectManager->FindObject(static_cast<Hash32>(prop));
}

static physics::Body* FindPropBody(int64 prop)
{
	Object* object = FindProp(prop);

	if (object == nullptr) {
		return nullptr;
	}

	return gPhysics->GetBody(object->PhysicsID);
}

/// Holds a prop at a velocity without letting contacts turn it. Call it every frame for a scripted mover.
static void N_prop_drive(int64 prop, FLOAT4 velocity)
{
	physics::Body* body = FindPropBody(prop);

	if (body == nullptr) {
		return;
	}

	JPH::BodyInterface& body_interface = gPhysics->pBackend->GetBodyInterface();
	const JPH::BodyID body_id = body->GetBodyID();

	body_interface.SetRotation(body_id, JPH::Quat::sIdentity(), JPH::EActivation::Activate);
	body->SetLinearVelocity(FVec3(velocity));
	body_interface.SetAngularVelocity(body_id, JPH::Vec3::sZero());
}

/// Read from the physics body, since an object's own position only updates while it is being drawn
static FLOAT4 N_prop_position(int64 prop)
{
	physics::Body* body = FindPropBody(prop);

	if (body == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return body->GetPosition().mIntrin;
}

static void N_prop_destroy(int64 prop)
{
	Object* object = FindProp(prop);

	if (object == nullptr) {
		return;
	}

	gPhysics->DestroyBody(object->PhysicsID);
	gWorld->Detach(object->ID);
	gObjectManager->DestroyObject(object->ID);
}

static void N_npc_behaviour_list(int32 npc)
{
	if (npc == 0) {
		gNPCManager->GetDefaultBehaviours().LogState();
		return;
	}

	if (NPC* found = FindNPC(npc)) {
		found->GetBehaviours().LogState();
	}
}

static void N_script_error(const char* str) { LogError(LC_SCRIPT, "{}", str); }

static void N_tool_state_send(const editor::EditorToolState* state)
{
#ifdef FX_IS_EDITOR
	if (state == nullptr) {
		return;
	}

	gEditor->SubmitToolState(*state);
#endif
}

static LightSpot* N_RLightSpot_Create(const char* name, eLightFlags light_flags)
{
	LightSpot* light = gLightManager->NewLight<LightSpot>(name);
	light->SetFlags(light_flags);
	return light;
}

static void N_RLightSpot_SetRadius(LightSpot* light, float32 radius) { light->SetRadius(radius); }
static float32 N_RLightSpot_GetRadius(LightSpot* light) { return light->GetRadius(); }

static void N_RLightSpot_SetConeAngles(LightSpot* light, float32 inner_rad, float32 outer_rad)
{
	light->SetConeAngles(inner_rad, outer_rad);
}

static void N_RLightSpot_SetPosition(LightSpot* light, FLOAT4 position) { light->SetPosition(FVec3(position)); }

static void N_RLightSpot_SetRotationEuler(LightSpot* light, FLOAT4 rotation_euler)
{
	light->SetRotation(FQuat::FromEulerAngles(FVec3(rotation_euler)));
}

static float32 N_RLightSpot_GetInnerAngle(LightSpot* light) { return light->GetInnerAngle(); }
static float32 N_RLightSpot_GetOuterAngle(LightSpot* light) { return light->GetOuterAngle(); }
static eLightFlags N_RLightSpot_GetFlags(LightSpot* light) { return light->Flags; }
static void N_RLightSpot_SetFlags(LightSpot* light, eLightFlags flags) { light->SetFlags(flags); }
static void N_RLightSpot_SetDirection(LightSpot* light, FLOAT4 direction) { light->SetDirection(FVec3(direction)); }

static void N_RLightSpot_SetColor(LightSpot* light, uint32 color) { light->SetColor(Color(color)); }
static uint32 N_RLightSpot_GetColor(LightSpot* light) { return light->GetColor().AsUInt(); }

static void N_RLightSpot_Destroy(LightSpot* light) { gLightManager->RemoveLight(light->ID); }


/////////////////////////////////////
// Predef gather
/////////////////////////////////////


#define PREDEF(name_, fn_)                                                                                             \
	PredefExtern { name_, reinterpret_cast<void*>(fn_) }

static const PredefExtern scAvailableExterns[] = {
	PREDEF("printf", printf),
	PREDEF("sqrtf", sqrtf),
	PREDEF("sinf", sinf),
	PREDEF("cosf", cosf),

	/* Object functions  */
	PREDEF("RObjectGet", N_RObjectGet),
	PREDEF("RObjectFind", N_RObjectFind),

	PREDEF("editor_push_op_vec", N_editor_push_op_vec3),
	PREDEF("editor_push_op_delete", N_editor_push_op_delete),
	PREDEF("editor_op_create_object", N_editor_op_create_object),
	PREDEF("editor_op_dupe_object", N_editor_op_dupe_object),
	PREDEF("editor_snap_selection_to_grid", N_editor_snap_selection_to_grid),

	PREDEF("RObject_MoveTo", N_RObject_MoveTo),
	PREDEF("RObject_MoveBy", N_RObject_MoveBy),
	PREDEF("RObject_GetPosition", N_RObject_GetPosition),
	PREDEF("RObject_RotateToEuler", N_RObject_RotateToEuler),
	PREDEF("RObject_GetRotationEuler", N_RObject_GetRotationEuler),
	PREDEF("RObject_GetTags", N_RObject_GetTags),
	PREDEF("RObject_SetTags", N_RObject_SetTags),

	PREDEF("RLightSpot_Create", N_RLightSpot_Create),
	PREDEF("RLightSpot_GetFlags", N_RLightSpot_GetFlags),
	PREDEF("RLightSpot_SetFlags", N_RLightSpot_SetFlags),
	PREDEF("RLightSpot_SetRadius", N_RLightSpot_SetRadius),
	PREDEF("RLightSpot_GetRadius", N_RLightSpot_GetRadius),
	PREDEF("RLightSpot_SetConeAngles", N_RLightSpot_SetConeAngles),
	PREDEF("RLightSpot_SetPosition", N_RLightSpot_SetPosition),
	PREDEF("RLightSpot_SetRotationEuler", N_RLightSpot_SetRotationEuler),
	PREDEF("RLightSpot_GetInnerAngle", N_RLightSpot_GetInnerAngle),
	PREDEF("RLightSpot_GetOuterAngle", N_RLightSpot_GetOuterAngle),
	PREDEF("RLightSpot_SetDirection", N_RLightSpot_SetDirection),
	PREDEF("RLightSpot_SetColor", N_RLightSpot_SetColor),
	PREDEF("RLightSpot_GetColor", N_RLightSpot_GetColor),
	PREDEF("RLightSpot_Destroy", N_RLightSpot_Destroy),

	PREDEF("blockout_reload_object", N_blockout_reload_object),
	PREDEF("blockout_object_scale", N_blockout_object_scale),
	PREDEF("blockout_face_center", N_blockout_face_center),
	PREDEF("blockout_edit_face_texture", N_blockout_edit_face_texture),
	PREDEF("blockout_ray_hit_point", N_blockout_ray_hit_point),
	PREDEF("blockout_preview_box", N_blockout_preview_box),
	PREDEF("blockout_preview_clip", N_blockout_preview_clip),
	PREDEF("blockout_hide_preview", N_blockout_hide_preview),
	PREDEF("blockout_create_box", N_blockout_create_box),
	PREDEF("blockout_clip", N_blockout_clip),
	PREDEF("blockout_subtract", N_blockout_subtract),
	PREDEF("camera_ray_to_plane", N_camera_ray_to_plane),
	PREDEF("blockout_new_object", N_blockout_new_object),
	PREDEF("blockout_dupe_object", N_blockout_dupe_object),
	PREDEF("blockout_destroy_object", N_blockout_destroy_object),

	PREDEF("camera_position", N_camera_position),

	PREDEF("PLAYER_get_position", N_player_get_position),
	PREDEF("PLAYER_set_speed_multiplier", N_player_set_speed_multiplier),
	PREDEF("PLAYER_is_flymode", N_player_is_flymode),
	PREDEF("PLAYER_ray_get_point", N_player_ray_get_point),
	PREDEF("PLAYER_ray_get_normal", N_player_ray_get_normal),

	/* Math Util */
	PREDEF("float4_round", N_float4_round),
	PREDEF("float4_floor", N_float4_floor),
	PREDEF("float3_abs", N_float3_abs),
	PREDEF("float_sign", N_float_sign),
	PREDEF("float_sqrt", N_float_sqrt),
	PREDEF("print_float4", N_print_float4),

	/* Controls */
	PREDEF("RKey_IsUp", N_RKey_IsUp),
	PREDEF("RKey_IsDown", N_RKey_IsDown),
	PREDEF("RKey_IsPressed", N_RKey_IsPressed),

	/* Reflection probes */
	PREDEF("probe_volume_mark", N_probe_volume_mark),
	PREDEF("probe_volume_is_marked", N_probe_volume_is_marked),
	PREDEF("probe_is_baking", N_probe_is_baking),
	PREDEF("probe_save", N_probe_save),
	PREDEF("reflection_probe_mark", N_reflection_probe_mark),
	PREDEF("reflection_probe_is_marked", N_reflection_probe_is_marked),
	PREDEF("reflection_probe_rebuild", N_reflection_probe_rebuild),
	PREDEF("reflection_probe_bake", N_reflection_probe_bake),

	PREDEF("cvar_set_int", N_cvar_set_int),
	PREDEF("cvar_set_float", N_cvar_set_float),
	PREDEF("cvar_set_string", N_cvar_set_string),

	PREDEF("cvar_get_int", N_cvar_get_int),
	PREDEF("cvar_get_float", N_cvar_get_float),

	/* New implementation */
	PREDEF("CVarInterop_SetInt", N_cvar_set_int),
	PREDEF("CVarInterop_SetFloat", N_cvar_set_float),
	PREDEF("CVarInterop_SetString", N_cvar_set_string),

	PREDEF("CVarInterop_GetInt", N_cvar_get_int),
	PREDEF("CVarInterop_GetFloat", N_cvar_get_float),


	PREDEF("fastrand32", N_fastrand32),
	PREDEF("random_range", N_random_range),

	PREDEF("particle_play", N_particle_play),
	PREDEF("particle_move", N_particle_move),
	PREDEF("particle_stop", N_particle_stop),
	PREDEF("particle_kill", N_particle_kill),
	PREDEF("particle_alive", N_particle_alive),

	PREDEF("sound_play", N_sound_play),
	PREDEF("sound_play_2d", N_sound_play_2d),
	PREDEF("sound_move", N_sound_move),
	PREDEF("sound_set_volume", N_sound_set_volume),
	PREDEF("sound_stop", N_sound_stop),
	PREDEF("sound_kill", N_sound_kill),
	PREDEF("sound_alive", N_sound_alive),
	PREDEF("sound_snapshot_start", N_sound_snapshot_start),
	PREDEF("sound_snapshot_stop", N_sound_snapshot_stop),

	PREDEF("timer_after", N_timer_after),
	PREDEF("timer_every", N_timer_every),
	PREDEF("timer_repeat", N_timer_repeat),
	PREDEF("timer_cancel", N_timer_cancel),
	PREDEF("timer_cancel_all", N_timer_cancel_all),
	PREDEF("timer_active", N_timer_active),
	PREDEF("timer_remaining", N_timer_remaining),
	PREDEF("timer_clock", N_timer_clock),
	PREDEF("timer_list", N_timer_list),

	PREDEF("portal_set_open", N_portal_set_open),
	PREDEF("portal_is_open", N_portal_is_open),

	PREDEF("RWeapon_WeaponTrace", N_RWeapon_WeaponTrace),
	PREDEF("RWeapon_ApplyHit", N_RWeapon_ApplyHit),
	PREDEF("RWeapon_DoWeaponKick", N_RWeapon_DoWeaponKick),
	PREDEF("RWeapon_HandleEvent", N_RWeapon_HandleEvent),

	PREDEF("npc_find", N_npc_find),
	PREDEF("npc_last_hit", N_npc_last_hit),
	PREDEF("npc_is_alive", N_npc_is_alive),
	PREDEF("npc_behaviour_trigger", N_npc_behaviour_trigger),
	PREDEF("npc_behaviour_stop", N_npc_behaviour_stop),
	PREDEF("npc_behaviour_active", N_npc_behaviour_active),
	PREDEF("npc_behaviour_set", N_npc_behaviour_set),
	PREDEF("npc_behaviour_get", N_npc_behaviour_get),
	PREDEF("npc_behaviour_list", N_npc_behaviour_list),
	PREDEF("npc_count", N_npc_count),
	PREDEF("npc_at", N_npc_at),
	PREDEF("npc_walk_to", N_npc_walk_to),
	PREDEF("npc_flee", N_npc_flee),
	PREDEF("npc_idle", N_npc_idle),
	PREDEF("npc_brain_state", N_npc_brain_state),
	PREDEF("npc_stats", N_npc_stats),
	PREDEF("npc_model_ready", N_npc_model_ready),
	PREDEF("npc_spawn", N_npc_spawn),
	PREDEF("npc_destroy", N_npc_destroy),
	PREDEF("npc_position", N_npc_position),
	PREDEF("npc_velocity", N_npc_velocity),
	PREDEF("npc_health", N_npc_health),
	PREDEF("npc_state", N_npc_state),

	PREDEF("prop_spawn_box", N_prop_spawn_box),
	PREDEF("prop_drive", N_prop_drive),
	PREDEF("prop_position", N_prop_position),
	PREDEF("prop_destroy", N_prop_destroy),

	PREDEF("script_error", N_script_error),

	PREDEF("tool_state_send", N_tool_state_send),

}; // namespace fx::script

Slice<const PredefExtern> GetInteropPredefs() { return Slice(scAvailableExterns, std::size(scAvailableExterns)); }


} // namespace fx::script
