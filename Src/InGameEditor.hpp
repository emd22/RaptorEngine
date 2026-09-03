#pragma once

#include <Core/Types.hpp>
#include <Renderer/Camera.hpp>
#include <Script/Script.hpp>
#include <World.hpp>

namespace fx {

enum class eEditorMode : int32
{
	Translate,
	/// The default mode (simulate) must ALWAYS be last, as it does not possess an EditorMode.
	Simulate,
};

enum class eEditorModeFlags
{
	None = (0),
	ConsumeInput = (1 << 0),
};

FxEnumFlags(eEditorModeFlags);

class EditorMode
{
public:
	EditorMode() = default;
	void Create(const String& script_path);
	void SelectObject(Object* object) const;
	~EditorMode() = default;

public:
	Ref<PerspectiveCamera> pCamera { nullptr };
	eEditorModeFlags Flags = eEditorModeFlags::None;

	script::Script* pScript = nullptr;
};


} // namespace fx
