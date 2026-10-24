#pragma once

#include <Core/Types.hpp>
#include <Math/SIMDHelper.hpp>
#include <cstddef>

namespace fx {

class Object;


namespace editor {

/// Mirrored by EDITORTOOL in `interop.strata`
enum class eEditorTool : uint32
{
	Select,
	Face,
	Vertex,
	Create,
	Arch,
	Room,
	Curve,
	Decal,
	Rotate,
	Clip,
	Light,

	Count,
};

enum class eEditorMode : uint32
{
	Vis,
	Data,

	Count,
};

enum class eEditorView : uint32
{
	Perspective,
	Top,
	Split,

	Count,
};

enum class eTopViewSelectMode : uint8
{
	Replace,
	Add,
	Toggle,
};

enum class eDataFilter : uint32
{
	All,
	ProbeVolumes,
	ReflectionProbes,
	SpawnPoints,
	Volumes,
	Lights,
	NvBrushes,
	DBounds,
	Portals,

	Count,
};

/// Mirrored by LIMIT_SELECTION_OBJECTS in `tool_common.strata`
static constexpr uint32 scMaxSelectedObjects = 128;

/**
 * @brief Configuration information to be sent to all editor scripts. Mirrored by TOOLSTATE in `tool_common.strata`.
 */
struct EditorToolState
{
	eEditorTool SelectedTool = eEditorTool::Select;

	// Snap / Quantization
	int32 ToolSnapLevel = 2;
	bool ToolSnapEnabled = true;

	Object* pTransformMarkerObject = nullptr;
};

static_assert(offsetof(EditorToolState, pTransformMarkerObject) == 16 && sizeof(EditorToolState) == 24,
			  "EditorToolState must match TOOLSTATE in tool_common.strata");

/**
 * @brief The selection as the editor command script sees it. Mirrored by SELECTION in `tool_common.strata`.
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


} // namespace editor
} // namespace fx
