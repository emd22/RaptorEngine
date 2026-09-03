#include "ScriptInterop.hpp"

#include <Engine.hpp>
#include <Math/SIMDHelper.hpp>
#include <Object/ObjectID.hpp>
#include <Object/ObjectManager.hpp>
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
	LogInfo(LC_SCRIPT, "Moving object to position {}", Vec3f(position));

	// if (obj == nullptr) {
	// 	return;
	// }

	// obj->SetPosition(Vec3f(position));
}

static void N_object_move_by(Object* obj, FLOAT4 by)
{
	LogInfo(LC_SCRIPT, "Moving object by {}", Vec3f(by));

	// if (obj == nullptr) {
	// 	return;
	// }

	// obj->SetPosition(Vec3f(position));
}


/////////////////////////////////////
// Predef gather
/////////////////////////////////////


#define PREDEF(name_, fn_)                                                                                             \
	PredefExtern { name_, reinterpret_cast<void*>(fn_) }

static const PredefExtern scAvailableExterns[] = {
	PREDEF("printf", printf),

	/* Object functions  */
	PREDEF("object_get", N_object_get),
	PREDEF("object_move_to", N_object_move_to),
	PREDEF("object_move_by", N_object_move_by),
};

Slice<const PredefExtern> GetInteropPredefs() { return Slice(scAvailableExterns, std::size(scAvailableExterns)); }


} // namespace fx::script
