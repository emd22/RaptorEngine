#include "EditorTool.hpp"

#include <Core/String.hpp>
#include <Engine.hpp>
#include <Script/Script.hpp>
#include <Script/ScriptManager.hpp>


namespace fx {


namespace editor {

EditorTool::EditorTool(eEditorTool tool_type, const char* script_path, eEditorToolFlags flags)
	: Tool(tool_type), Flags(flags)
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

	pFnEnter = pScript->GetFunction<void()>("tool_enter");
	pFnLeave = pScript->GetFunction<void()>("tool_leave");
	pFnBegin = pScript->GetFunction<void()>("tool_begin");
	pFnUpdate = pScript->GetFunction<void(float32)>("tool_update");
	pFnFinalize = pScript->GetFunction<void()>("tool_finalize");
	pFnControls = pScript->GetFunction<void()>("tool_controls");

	pFnStateReceive = pScript->GetFunction<void(const EditorToolState*)>("tool_state_receive");
	pFnSelectionReceive = pScript->GetFunction<void(const EditorToolSelection*)>("tool_selection_receive");
}

void EditorTool::Sync(const EditorToolState& state, const EditorToolSelection& selection)
{
	if (pFnStateReceive != nullptr) {
		pScript->CallFunctionPtr<void(const EditorToolState*)>(pFnStateReceive, &state);
	}

	if (pFnSelectionReceive != nullptr) {
		pScript->CallFunctionPtr<void(const EditorToolSelection*)>(pFnSelectionReceive, &selection);
	}
}

void EditorTool::Enter()
{
	if (pNative != nullptr) {
		pNative->Enter();
		return;
	}

	if (pFnEnter != nullptr) {
		pScript->CallFunctionPtr<void()>(pFnEnter);
	}
}

void EditorTool::Leave()
{
	if (pNative != nullptr) {
		pNative->Leave();
		return;
	}

	if (pFnLeave != nullptr) {
		pScript->CallFunctionPtr<void()>(pFnLeave);
	}
}

void EditorTool::Begin()
{
	if (pNative != nullptr) {
		pNative->Begin();
		return;
	}

	if (pFnBegin != nullptr) {
		pScript->CallFunctionPtr<void()>(pFnBegin);
	}
}

void EditorTool::Update(float32 delta_time)
{
	if (pNative != nullptr) {
		pNative->Update(delta_time);
		return;
	}

	if (pFnUpdate != nullptr) {
		pScript->CallFunctionPtr<void(float32)>(pFnUpdate, delta_time);
	}
}

void EditorTool::Finalize()
{
	if (pNative != nullptr) {
		pNative->Finalize();
		return;
	}

	if (pFnFinalize != nullptr) {
		pScript->CallFunctionPtr<void()>(pFnFinalize);
	}
}

void EditorTool::Cancel()
{
	if (pNative != nullptr) {
		pNative->Cancel();
	}
}

void EditorTool::Controls()
{
	if (pNative != nullptr) {
		pNative->Controls();
		return;
	}

	if (pFnControls != nullptr) {
		pScript->CallFunctionPtr<void()>(pFnControls);
	}
}

EditorTool::~EditorTool()
{
	// Scripts are destroyed by the script manager, just invalidate it here.
	pScript = nullptr;
}

} // namespace editor

} // namespace fx
