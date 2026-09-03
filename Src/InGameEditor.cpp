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

void EditorMode::Load()
{
	auto mode_load = pScript->GetFunction<void (*)()>("mode_load");
	if (mode_load) {
		mode_load();
	}

	ReloadHotFunctions();
}

void EditorMode::Unload()
{
	auto mode_unload = pScript->GetFunction<void (*)()>("mode_unload");
	if (mode_unload) {
		mode_unload();
	}
}


} // namespace fx
