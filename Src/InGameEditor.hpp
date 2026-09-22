#pragma once

#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <Core/UndoStack.hpp>
#include <Material/MaterialID.hpp>
#include <Math/Quat.hpp>
#include <Math/SIMDHelper.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <Renderer/Camera.hpp>
#include <Script/Script.hpp>
#include <World.hpp>

namespace fx {

enum class eEditorTool : uint32
{
	None,

	Translate,
	Face,
	Rotate,

	Count,
};


/// TODO: remove
enum class eEditorState : int32
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
		Dupe,
		Create,
		Delete,
		/// ValueA/ValueB hold Euler angles in radians (Quat::GetEulerAngles()/FromEulerAngles() convention)
		Rotate,
	} Type;

public:
	void Undo();

	EditOperationValue Execute();

public:
	/// The object to manipulate
	Object* pObject = nullptr;

	ObjectID PushedObjectID = ObjectID::scNull;

	/// Stable ID for `ValueB` when it holds an object (Dupe result).
	ObjectID ValueObjectID = ObjectID::scNull;

	EditOperationValue ValueA;
	EditOperationValue ValueB;

	/// Pre-apply snapshot for `Scale` (bounds + position), so Undo restores exactly
	/// even when the min-thickness push-through translated the object.
	/// (Negating the magnitude alone would mis-split travel between bounds/shift.)
	Vec3f ScaleBoundsMinBefore = Vec3f::sZero;
	Vec3f ScaleBoundsMaxBefore = Vec3f::sZero;
	Vec3f ScalePosBefore = Vec3f::sZero;

	/// Full snapshot for `Delete` (captured before destruction so Undo can recreate).
	struct Snapshot
	{
		Vec3f Position = Vec3f::sZero;
		Vec3f BoundsMin = Vec3f::sZero;
		Vec3f BoundsMax = Vec3f::sZero;
		MaterialID Material = MaterialID::scNull;
		Quat Rotation = Quat::scIdentity;
		Name ObjectName;
		/// So that undoing the delete of a probe volume brush brings back a probe volume, not solid geometry
		bool bIsProbeVolume = false;
	} DeleteSnapshot;

	/// The size of the operation group this is in. For example, when moving 10 objects, there will be 10 operations(one
	/// for each event) making the GroupSize = 10.
	int32 GroupSize = 1;
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

	friend struct EditOperation;

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

	void ResetUndoStack();

	Object* GetLastSelectedObject();

	uint32 SelectedCount() const { return mSelectedObjects.Size; }

	void Reload();
	void Unload();
	float GetQuantizeFraction() const;
	bool GetQuantizeEnabled() const;

	EditOperationValue PushEditOperation(const EditOperation& op);

	/// Original (unselected) material for an object, falling back to its current material.
	MaterialID GetStoredMaterial(Object* object);

	void SetStoredMaterial(Object* object, MaterialID material);

	void Undo();
	void Redo();

	FX_FORCE_INLINE bool HasSelection() const { return (mSelectedObjects.Size > 0); }

	bool IsInSelection(Object* object) const;


	~EditorMode() = default;

private:
	/// C++-only selection mutation (no script callback). Caller must SyncScriptSelection().
	void AddToSelectionInternal(Object* object);
	void RemoveFromSelectionInternal(Object* object);
	void SyncScriptSelection();

	static bool OpTouchesSelection(EditOperation::eType type)
	{
		return type == EditOperation::eType::Dupe || type == EditOperation::eType::Create ||
			   type == EditOperation::eType::Delete;
	}

public:
	eEditorModeFlags Flags = eEditorModeFlags::None;

	String ModeName;

	UpdateFnDef pUpdateFunction = nullptr;

	// MaterialID mSelectedObjectPreviousMaterial = MaterialID::scNull;
	// Object* mpLastSelectedObject = nullptr;


	StackArray<SelectedObject, scLimitSelectionObjects> mSelectedObjects;

	UndoStack<EditOperation> mOperationStack;

	// uint32 OperationStackIndex = 0;

	script::Script* pScript = nullptr;
};


} // namespace fx
