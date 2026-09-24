#include "EditorTool.hpp"

#include <Core/String.hpp>
#include <Engine.hpp>
#include <Script/Script.hpp>
#include <Script/ScriptManager.hpp>


namespace fx {


namespace editor {

EditorTool::EditorTool(eEditorTool tool_type, const char* script_path) : Tool(tool_type)
{
	if (script_path == nullptr) {
		pScript = nullptr;
		return;
	}

	pScript = gScriptManager->LoadScript(script_path);

	ReloadHotFunctions();
}

void EditorTool::ReloadHotFunctions()
{
	if (pScript == nullptr) {
		return;
	}

	pFnBegin = pScript->GetFunction<void (*)(void*)>("tool_begin");
	pFnUpdate = pScript->GetFunction<void (*)(void*, float)>("tool_update");
	pFnFinalize = pScript->GetFunction<void (*)(void*)>("tool_finalize");
}

EditorTool::~EditorTool()
{
	// Scripts are destroyed by the script manager, just invalidate it here.
	pScript = nullptr;
}

} // namespace editor

} // namespace fx
