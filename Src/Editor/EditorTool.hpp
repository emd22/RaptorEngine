#pragma once

#include <Core/Types.hpp>

namespace fx {

class String;

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


struct EditorTool
{
public:
	EditorTool() = default;
	EditorTool(const eEditorTool tool_type, const char* script_path);

	~EditorTool();

public:
	script::Script* pScript = nullptr;
	eEditorTool Tool = eEditorTool::None;
};


} // namespace editor
} // namespace fx
