#pragma once

#include <Core/Types.hpp>
#include <Math/SIMDHelper.hpp>
#include <Script/Script.hpp>
#include <cstddef>

namespace fx {

class String;
class Object;


namespace editor {

/// Mirrored by EDITORTOOL in `interop.strata`
enum class eEditorTool : uint32
{
	None,

	Translate,
	Face,
	Rotate,
	Create,
	Clip,
	SetMaterial,

	Count,
};

enum class eEditorToolFlags : uint32
{
	None = 0,

	/// The tool works on the selection, so clicking picks objects and a drag needs something selected. Tools
	/// without it work on whatever is under the crosshair instead.
	UsesSelection = (1 << 0),
};

} // namespace editor

FxEnumFlags(editor::eEditorToolFlags);

namespace editor {

/// Mirrored by LIMIT_SELECTION_OBJECTS in `tool_common.strata`
static constexpr uint32 scMaxSelectedObjects = 64;

/**
 * @brief Configuration information to be sent to all editor scripts. Mirrored by TOOLSTATE in `tool_common.strata`.
 */
struct EditorToolState
{
	eEditorTool SelectedTool = eEditorTool::None;

	// Snap / Quantization
	int32 ToolSnapLevel = 2;
	bool ToolSnapEnabled = true;

	Object* pTransformMarkerObject = nullptr;
};

static_assert(offsetof(EditorToolState, pTransformMarkerObject) == 16 && sizeof(EditorToolState) == 24,
			  "EditorToolState must match TOOLSTATE in tool_common.strata");

/**
 * @brief The selection as the editor scripts see it, along with where each object was when the current drag started.
 * Mirrored by SELECTION in `tool_common.strata`.
 */
struct EditorToolSelection
{
	Object* pSelection[scMaxSelectedObjects];
	FLOAT4 pInitialObjectPositions[scMaxSelectedObjects];
	FLOAT4 pInitialObjectRotations[scMaxSelectedObjects];

	int32 SelectionSize = 0;
};

static_assert(offsetof(EditorToolSelection, SelectionSize) == scMaxSelectedObjects * 40 &&
				  sizeof(EditorToolSelection) == scMaxSelectedObjects * 40 + 16,
			  "EditorToolSelection must match SELECTION in tool_common.strata");


/**
 * @brief An editor tool, run by a script in `Scripts/editor/tools/`. The script can provide any of:
 *
 * - `tool_enter()` / `tool_leave()`: the tool was selected / another tool was selected
 * - `tool_begin()`: the mouse was pressed, starting a drag
 * - `tool_update(float delta_time)`: every frame of the drag
 * - `tool_finalize()`: the mouse was released, ending the drag
 * - `tool_controls()`: every frame that the tool is selected and there is no drag, for the tool's own hotkeys
 *
 * The editor state and selection are sent to the script (see `tool_common.strata`) before any of these are called.
 */
struct EditorTool
{
public:
	EditorTool() = default;
	EditorTool(const eEditorTool tool_type, const char* script_path, eEditorToolFlags flags);

	/**
	 * @brief Reload functions from the script files. Call after the script is hotreloaded.
	 */
	void ReloadHotFunctions();

	/// Copies the editor's state and selection into the script
	void Sync(const EditorToolState& state, const EditorToolSelection& selection);

	void Enter();
	void Leave();

	void Begin();
	void Update(float32 delta_time);
	void Finalize();

	void Controls();

	FX_FORCE_INLINE bool UsesSelection() const { return HasFlag(Flags, eEditorToolFlags::UsesSelection); }

	~EditorTool();

public:
	script::Script* pScript = nullptr;
	eEditorTool Tool = eEditorTool::None;
	eEditorToolFlags Flags = eEditorToolFlags::None;

private:
	/////////////////////////////////////
	// Cached hot functions
	/////////////////////////////////////

	script::ScriptFunctionType<void()> pFnEnter = nullptr;
	script::ScriptFunctionType<void()> pFnLeave = nullptr;
	script::ScriptFunctionType<void()> pFnBegin = nullptr;
	script::ScriptFunctionType<void(float32)> pFnUpdate = nullptr;
	script::ScriptFunctionType<void()> pFnFinalize = nullptr;
	script::ScriptFunctionType<void()> pFnControls = nullptr;

	script::ScriptFunctionType<void(const EditorToolState*)> pFnStateReceive = nullptr;
	script::ScriptFunctionType<void(const EditorToolSelection*)> pFnSelectionReceive = nullptr;
};


} // namespace editor
} // namespace fx
