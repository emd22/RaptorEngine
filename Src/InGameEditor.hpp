#pragma once

#include <Core/Types.hpp>
#include <Math/SIMDHelper.hpp>
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
	using UpdateFnDef = void (*)(FLOAT4);

public:
	EditorMode() = default;

	void Create(const String& name, const String& script_path);
	bool SelectObject(Object* object);
	void Update(const Vec3f& movement_vector) const;
	void ReloadHotFunctions();

	void Load();
	void Unload();
	float GetQuantizeFraction() const;

	~EditorMode() = default;

public:
	eEditorModeFlags Flags = eEditorModeFlags::None;

	String ModeName;

	UpdateFnDef pUpdateFunction = nullptr;

	MaterialID mSelectedObjectPreviousMaterial = MaterialID::scNull;
	Object* mpLastSelectedObject = nullptr;

	script::Script* pScript = nullptr;
};


} // namespace fx
