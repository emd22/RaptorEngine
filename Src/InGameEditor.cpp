#include "InGameEditor.hpp"

#include <Engine.hpp>
#include <Script/ScriptManager.hpp>

namespace fx {

/////////////////////////////////////
// Translate Object
/////////////////////////////////////

void EditorMode::Create(const String& script_path)
{
	pScript = gScriptManager->LoadScript(script_path.CStr());

	// auto mode_load = pScript->GetFunction<void (*)()>("mode_load");
	// if (mode_load) {
	// 	mode_load();
	// }
}

void EditorMode::SelectObject(Object* object) const
{
	LogInfo(LC_SCRIPT, "Select object");

	if (pScript == nullptr) {
		return;
	}

	LogInfo(LC_SCRIPT, "Select object 2");

	auto mode_select_object = pScript->GetFunction<void (*)(void*)>("mode_select_object");
	if (mode_select_object) {
		mode_select_object(reinterpret_cast<void*>(object));
	}
}


} // namespace fx
