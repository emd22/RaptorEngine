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

	pFnBegin = pScript->GetFunction<void()>("tool_begin");
	pFnUpdate = pScript->GetFunction<void(float)>("tool_update");
	pFnFinalize = pScript->GetFunction<void()>("tool_finalize");
	pFnGetSelection = pScript->GetFunction<void*()>("_tool_get_selection");
}

EditorToolSelection* EditorTool::RetrieveSelection()
{
	if (pFnGetSelection == nullptr) {
		return nullptr;
	}

	return reinterpret_cast<EditorToolSelection*>(pScript->CallFunctionPtr<void*()>(pFnGetSelection));
}

EditorTool::~EditorTool()
{
	// Scripts are destroyed by the script manager, just invalidate it here.
	pScript = nullptr;
}

} // namespace editor

} // namespace fx
