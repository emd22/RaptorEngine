#include "InGameEditor.hpp"

#include <Engine.hpp>
#include <Script/ScriptManager.hpp>

namespace fx {

/////////////////////////////////////
// Translate Object
/////////////////////////////////////

void EditorMode::Create(const String& name, const String& script_path)
{
	ModeName = name;
	pScript = gScriptManager->LoadScript(script_path.CStr());

	auto mode_load = pScript->GetFunction<void (*)()>("mode_load");
	if (mode_load) {
		mode_load();
	}
	ReloadHotFunctions();
}

void EditorMode::ReloadHotFunctions() { pUpdateFunction = pScript->GetFunction<UpdateFnDef>("mode_update"); }

void EditorMode::Update(const Vec3f& movement_vector) const
{
	if (pUpdateFunction) {
		pUpdateFunction(movement_vector.mIntrin);
	}
}

void EditorMode::SelectObject(Object* object) const
{
	if (pScript == nullptr) {
		return;
	}

	auto mode_select_object = pScript->GetFunction<void (*)(void*)>("mode_select_object");
	if (mode_select_object) {
		mode_select_object(reinterpret_cast<void*>(object));
	}
}


} // namespace fx
