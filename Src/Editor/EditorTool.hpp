#pragma once

#include <Core/Types.hpp>
#include <Math/SIMDHelper.hpp>
#include <Script/Script.hpp>
#include <cstddef>
#include <functional>

namespace fx {

class String;
class Object;


namespace editor {

class ToolSettingsBasePanel;

/// Mirrored by EDITORTOOL in `interop.strata`
enum class eEditorTool : uint32
{
	None,

	Translate,
	Face,
	Rotate,
	Create,
	Clip,
	Light,

	Count,
};

enum class eEditorToolFlags : uint32
{
	None = 0,

	/// The tool works on the selection, so clicking picks objects and a drag needs something selected
	UsesSelection = (1 << 0),

	/// The tool picks something other than objects, so the object selection is dropped when it is selected
	ClearsSelection = (1 << 1),

	UsesModels = (1 << 2),
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
 * @brief A tool that is written in C++ instead of as a script. This is for tools that touch more engine than the
 * interop API provides.
 */
class NativeEditorTool
{
public:
	virtual ~NativeEditorTool() = default;

	virtual void Enter() {}
	virtual void Leave() {}

	virtual void Begin() {}
	virtual void Update(float32 delta_time) {}
	virtual void Finalize() {}

	/// The drag was thrown away without a Finalize(), because what it works on is going away
	virtual void Cancel() {}

	virtual void Controls() {}
};


struct EditorTool
{
public:
	EditorTool() = default;
	EditorTool(const eEditorTool tool_type, const char* script_path, eEditorToolFlags flags);

	/**
	 * @brief Reload functions from the script files. Call after the script is hotreloaded.
	 */
	void ReloadHotFunctions();

	/**
	 * @brief Copies the editor's state and selection into the script
	 */
	void Sync(const EditorToolState& state, const EditorToolSelection& selection);

	void Enter();
	void Leave();

	void Begin();
	void Update(float32 delta_time);
	void Finalize();
	void Cancel();

	void Controls();

	void SetNative(NativeEditorTool* native) { pNative = native; }

	/// Registers how to build this tool's panel in the Tool Settings slot below Object Properties. Pass nullptr
	/// (the default) for a tool with no settings panel.
	void SetSettingsPanel(std::function<ToolSettingsBasePanel*()> factory)
	{
		SettingsPanelFactory = std::move(factory);
	}

	/// Builds this tool's settings panel, or returns nullptr if it has none
	ToolSettingsBasePanel* CreateSettingsPanel() const
	{
		return SettingsPanelFactory ? SettingsPanelFactory() : nullptr;
	}

	FX_FORCE_INLINE bool UsesSelection() const { return HasFlag(Flags, eEditorToolFlags::UsesSelection); }
	FX_FORCE_INLINE bool UsesModels() const { return HasFlag(Flags, eEditorToolFlags::UsesModels); }

	~EditorTool();

public:
	script::Script* pScript = nullptr;
	NativeEditorTool* pNative = nullptr;
	eEditorTool Tool = eEditorTool::None;
	eEditorToolFlags Flags = eEditorToolFlags::None;

private:
	std::function<ToolSettingsBasePanel*()> SettingsPanelFactory = nullptr;

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
