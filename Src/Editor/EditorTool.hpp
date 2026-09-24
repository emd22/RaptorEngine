#pragma once

#include <Core/Types.hpp>
#include <Math/SIMDHelper.hpp>

namespace fx {

class String;
class Object;

namespace script {
class Script;
}


namespace editor {

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

static constexpr uint32 scMaxSelectedObjects = 64;

/**
 * @brief Configuration information to be sent to all editor scripts.
 */
struct EditorToolState
{
	eEditorTool SelectedTool = eEditorTool::None;

	// Snap / Quantization
	int ToolSnapLevel = 2;
	bool ToolSnapEnabled = true;

	Object* pTransformMarkerObject = nullptr;
};


struct EditorToolSelection
{
	Object* pSelection[scMaxSelectedObjects];
	FLOAT4 pInitialObjectPositions[scMaxSelectedObjects];
	FLOAT4 pInitialObjectRotations[scMaxSelectedObjects];

	int SelectionSize = 0;

	FLOAT4 SelectedFace;
	float32 SelectedFaceScale = 1.0f;
};


struct EditorTool
{
public:
	EditorTool() = default;
	EditorTool(const eEditorTool tool_type, const char* script_path);

	/**
	 * @brief Reload functions from the script files. Call after the script is hotreloaded.
	 */
	void ReloadHotFunctions();

	~EditorTool();

public:
	script::Script* pScript = nullptr;
	eEditorTool Tool = eEditorTool::None;

private:
	/////////////////////////////////////
	// Cached hot functions
	/////////////////////////////////////

	void (*pFnBegin)(void*) = nullptr;
	void (*pFnUpdate)(void*, float) = nullptr;
	void (*pFnFinalize)(void*) = nullptr;
};


} // namespace editor
} // namespace fx
