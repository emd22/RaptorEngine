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

	LogInfo("move by {}", Vec3f(by));

	obj->MoveBy(Vec3f(by));
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
	PREDEF("object_move_to", N_object_move_to),
	PREDEF("object_move_by", N_object_move_by),
};

Slice<const PredefExtern> GetInteropPredefs() { return Slice(scAvailableExterns, std::size(scAvailableExterns)); }


} // namespace fx::script
