#pragma once

#include <Core/DynArray.hpp>
#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <Math/SIMDHelper.hpp>
#include <Renderer/Camera.hpp>
#include <Script/Script.hpp>
#include <World.hpp>

namespace fx {

enum class eEditorMode : int32
{
	// Translate,
	// Scale,
	IGE,
	/// The default mode (simulate) must ALWAYS be last, as it does not possess an EditorMode.
	Simulate,
};


struct EditOperationValue
{
	enum class eValueType
	{
		Vec3,
		Object,
	} Type;

	union
	{
		Object* pObject;
		Vec3f Position;
	};


	explicit EditOperationValue(const Vec3f& vec) : Type(eValueType::Vec3), Position(vec) {}
	explicit EditOperationValue(Object* obj) : Type(eValueType::Object), pObject(obj) {}


	void Set(const Vec3f& vec)
	{
		Type = eValueType::Vec3;
		Position = vec;
	}

	void Set(Object* obj)
	{
		Type = eValueType::Object;
		pObject = obj;
	}
};

struct EditOperation
{
	enum class eType
	{
		Move,
		Scale,
		DupeObject,
	} Type;

public:
	void Undo();

	void Execute();

public:
	/// The object to manipulate
	Object* pObject = nullptr;

	EditOperationValue Original;
	EditOperationValue Updated;
};

enum class eEditorModeFlags
{
	None = (0),
	ConsumeInput = (1 << 0),
};

FxEnumFlags(eEditorModeFlags);

class EditorMode
{
	using UpdateFnDef = void (*)(FLOAT4, float32);

	/// The max amount of objects that can be selected. Mirrored in `prototype_editor.strata`
	static constexpr uint32 scLimitSelectionObjects = 64;

	struct SelectedObject
	{
		Object* pObject = nullptr;
		MaterialID OldMaterial = MaterialID::scNull;
	};

public:
	EditorMode() = default;

	void Create(const String& name, const String& script_path);
	bool SelectObject(Object* object, bool append_selection);
	void Update(const Vec3f& movement_vector, float32 delta_time);
	void ReloadHotFunctions();

	Object* GetLastSelectedObject();

	uint32 SelectedCount() const { return mSelectedObjects.Size; }

	void Load();
	void Unload();
	float GetQuantizeFraction() const;
	bool GetQuantizeEnabled() const;

	void PushEditOperation(const EditOperation& op)
	{
		mOperationStack.Insert(op);
		mOperationStack[mOperationStack.Size - 1].Execute();
	}

	void Undo();

	FX_FORCE_INLINE bool HasSelection() const { return (mSelectedObjects.Size > 0); }

	bool IsInSelection(Object* object) const;

	~EditorMode() = default;

public:
	eEditorModeFlags Flags = eEditorModeFlags::None;

	String ModeName;

	UpdateFnDef pUpdateFunction = nullptr;

	// MaterialID mSelectedObjectPreviousMaterial = MaterialID::scNull;
	// Object* mpLastSelectedObject = nullptr;


	StackArray<SelectedObject, scLimitSelectionObjects> mSelectedObjects;
	DynArray<EditOperation> mOperationStack;

	script::Script* pScript = nullptr;
};


} // namespace fx
