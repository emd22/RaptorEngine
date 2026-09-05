#include "ScriptInterop.hpp"

#include <Controls.hpp>
#include <Engine.hpp>
#include <Math/SIMDHelper.hpp>
#include <Object/ObjectID.hpp>
#include <Object/ObjectManager.hpp>
#include <World.hpp>
#include <cstdio>

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

static uint32 N_object_get_tags(Object* obj)
{
	if (obj == nullptr) {
		return 0;
	}

	return static_cast<uint32>(obj->Tags);
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
static void N_player_toggle_headbob(void*, bool value) { gWorld->Player.bEnableHeadBob = value; }
static bool N_player_get_headbob(void) { return gWorld->Player.bEnableHeadBob; }


static FLOAT4 N_float3_round(FLOAT4 value) { return simd::Round(value); }


static bool N_is_key_up(uint32 key) { return ControlManager::IsKeyUp(static_cast<eKey>(key)); }
static bool N_is_key_down(uint32 key) { return ControlManager::IsKeyDown(static_cast<eKey>(key)); }
static bool N_is_key_pressed(uint32 key) { return ControlManager::IsKeyPressed(static_cast<eKey>(key)); }


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

	PREDEF("camera_position", N_camera_position),

	PREDEF("PLAYER_get_position", N_player_get_position),
	PREDEF("PLAYER_set_speed_multiplier", N_player_set_speed_multiplier),
	PREDEF("PLAYER_toggle_headbob", N_player_toggle_headbob),
	PREDEF("PLAYER_get_headbob", N_player_get_headbob),

	PREDEF("float3_round", N_float3_round),

	/* Controls */
	PREDEF("KEY_is_up", N_is_key_up),
	PREDEF("KEY_is_down", N_is_key_down),
	PREDEF("KEY_is_pressed", N_is_key_pressed),
};

Slice<const PredefExtern> GetInteropPredefs() { return Slice(scAvailableExterns, std::size(scAvailableExterns)); }


} // namespace fx::script
