#include "ScriptInterop.hpp"

#include <Blockout.hpp>
#include <CVar.hpp>
#include <Controls.hpp>
#include <Engine.hpp>
#include <InGameEditor.hpp>
#include <Math/SIMDHelper.hpp>
#include <Object/ObjectID.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <World.hpp>

namespace fx::script {

static Object* N_object_get(uint32 id)
{
	ObjectID obj_id(id);
	if (obj_id.IsInvalid()) {
		return nullptr;
	}

	return gObjectManager->GetObject(obj_id);
}


static void N_object_move_to(Object* obj, FLOAT4 position)
{
	if (obj == nullptr) {
		return;
	}


	obj->SetPosition(Vec3f(position));
}

static void N_object_move_by(Object* obj, FLOAT4 by)
{
	if (obj == nullptr) {
		return;
	}

	obj->MoveBy(Vec3f(by));
}


static FLOAT4 N_object_get_position(Object* obj)
{
	if (obj == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return obj->GetPosition().mIntrin;
}

static void N_print_float4(FLOAT4 v) { LogInfo(LC_SCRIPT, "{}", Vec4f(v)); }

static uint32 N_object_get_tags(Object* obj)
{
	if (obj == nullptr) {
		return 0;
	}

	return static_cast<uint32>(obj->Tags);
}


static float32 N_object_direction_scale(Object* obj, FLOAT4 direction)
{
	if (obj == nullptr) {
		return 0.0f;
	}

	// const Vec3f dir = Vec3f(direction).Normalize();
	// Vec3f extent((dir.X >= 0.0f) ? pos_extent.X : neg_extent.X, (dir.Y >= 0.0f) ? pos_extent.Y : neg_extent.Y,
	// 			 (dir.Z >= 0.0f) ? pos_extent.Z : neg_extent.Z);

	// float32 distance = dir.Abs().Dot(extent);
	// return distance * mScale;

	return obj->GetDirectionScale(Vec3f(direction));
}

static FLOAT4 N_object_ray_get_face(Object* obj)
{
	if (obj == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	physics::Body* body = gPhysics->GetBody(obj->PhysicsID);
	if (body == nullptr) {
		return simd::LoadFloat4(0.0f);
	}

	return gPhysics->pBackend->RaycastGetFaceOfBox(body->GetBody(), gWorld->Player.pCamera->Position,
												   gWorld->Player.pCamera->GetForwardVector() * 4.0f);
}

static void N_object__select_object_internal(Object* obj, bool is_selected)
{
	if (gSelectedEditorMode == nullptr) {
		return;
	}

	// Deselect object
	if (!is_selected || obj == nullptr) {
		gSelectedEditorMode->SelectObject(nullptr);
		return;
	}

	// Select an object
	gSelectedEditorMode->SelectObject(obj);
}


static uint32 N_ctrl_mouse_state()
{
	uint32 result = 0;
	if (ControlManager::IsKeyDown(eKey::FX_MOUSE_LEFT)) {
		result |= (1 << 0);
	}
	if (ControlManager::IsKeyDown(eKey::FX_MOUSE_RIGHT)) {
		result |= (1 << 1);
	}

	return result;
}


static FLOAT4 N_camera_position() { return gWorld->GetCurrentCamera()->Position.mIntrin; }
static FLOAT4 N_player_get_position(void*) { return gWorld->Player.Position.mIntrin; }

static void N_player_set_speed_multiplier(void*, float mult) { gWorld->Player.SpeedMultiplier = mult; }
static bool N_player_is_flymode(void*) { return gWorld->Player.IsFlyMode(); }

static FLOAT4 N_player_ray_get_point(void*, float32 range)
{
	physics::RayResult rr = gPhysics->pBackend->Raycast(gWorld->Player.pCamera->Position,
														gWorld->Player.pCamera->Direction * range);

	if (!rr.bHit) {
		return simd::LoadFloat4(0.0f);
	}

	return rr.Point.mIntrin;
}

static FLOAT4 N_float4_round(FLOAT4 value) { return simd::Round(value); }
static FLOAT4 N_float3_abs(FLOAT4 value)
{
#ifdef FX_USE_NEON
	return Neon::SetSigns<1>(value);
#elif FX_USE_AVX
	return SSE::SetSigns<1>(value);
#endif
}


static bool N_is_key_up(uint32 key) { return ControlManager::IsKeyUp(static_cast<eKey>(key)); }
static bool N_is_key_down(uint32 key) { return ControlManager::IsKeyDown(static_cast<eKey>(key)); }
static bool N_is_key_pressed(uint32 key) { return ControlManager::IsKeyPressed(static_cast<eKey>(key)); }

static float N_float_sign(float value) { return MathUtil::GetSign(value); }

static void N_blockout_reload_object(Object* object) { gWorld->pBlockout->RebuildObject(object); }

static void N_blockout_object_scale(Object* object, FLOAT4 face_dir, FLOAT4 magnitude)
{
	gWorld->pBlockout->ScaleInDirection(object, Vec3f(face_dir), Vec3f(magnitude));
}

static Object* N_blockout_new_object(FLOAT4 position) { return gWorld->pBlockout->NewObject(Vec3f(position)); }
static Object* N_blockout_dupe_object(Object* object) { return gWorld->pBlockout->DupeObject(object); }
static void N_blockout_destroy_object(Object* object) { gWorld->pBlockout->DestroyObject(object); }


static void N_cvar_set_int(const char* name, int64 value) { gCVars->Set(name, value); }
static void N_cvar_set_float(const char* name, float32 value) { gCVars->Set(name, value); }
static void N_cvar_set_string(const char* name, const char* value) { gCVars->Set(name, value); }

static int64 N_cvar_get_int(const char* name, int64 fallback) { return gCVars->Get(name, fallback); }

/////////////////////////////////////
// Predef gather
/////////////////////////////////////


#define PREDEF(name_, fn_)                                                                                             \
	PredefExtern { name_, reinterpret_cast<void*>(fn_) }

static const PredefExtern scAvailableExterns[] = {
	PREDEF("printf", printf),

	/* Controls */
	PREDEF("ctrl_mouse_state", N_ctrl_mouse_state),

	/* Object functions  */
	PREDEF("object_get", N_object_get),
	PREDEF("OBJECT_move_to", N_object_move_to),
	PREDEF("OBJECT_move_by", N_object_move_by),
	PREDEF("OBJECT_get_position", N_object_get_position),
	PREDEF("OBJECT_get_tags", N_object_get_tags),
	PREDEF("OBJECT_ray_get_face", N_object_ray_get_face),
	PREDEF("OBJECT_direction_scale", N_object_direction_scale),
	PREDEF("OBJECT__select_object_internal", N_object__select_object_internal),

	PREDEF("blockout_reload_object", N_blockout_reload_object),
	PREDEF("blockout_object_scale", N_blockout_object_scale),
	PREDEF("blockout_new_object", N_blockout_new_object),
	PREDEF("blockout_dupe_object", N_blockout_dupe_object),
	PREDEF("blockout_destroy_object", N_blockout_destroy_object),

	PREDEF("camera_position", N_camera_position),

	PREDEF("PLAYER_get_position", N_player_get_position),
	PREDEF("PLAYER_set_speed_multiplier", N_player_set_speed_multiplier),
	PREDEF("PLAYER_is_flymode", N_player_is_flymode),
	PREDEF("PLAYER_ray_get_point", N_player_ray_get_point),

	/* Math Util */
	PREDEF("float4_round", N_float4_round),
	PREDEF("float3_abs", N_float3_abs),
	PREDEF("float_sign", N_float_sign),
	PREDEF("print_float4", N_print_float4),

	/* Controls */
	PREDEF("KEY_is_up", N_is_key_up),
	PREDEF("KEY_is_down", N_is_key_down),
	PREDEF("KEY_is_pressed", N_is_key_pressed),

	PREDEF("cvar_set_int", N_cvar_set_int),
	PREDEF("cvar_set_float", N_cvar_set_float),
	PREDEF("cvar_set_string", N_cvar_set_string),

	PREDEF("cvar_get_int", N_cvar_get_int),


}; // namespace fx::script

Slice<const PredefExtern> GetInteropPredefs() { return Slice(scAvailableExterns, std::size(scAvailableExterns)); }


} // namespace fx::script
