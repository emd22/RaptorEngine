#pragma once

#include <Brush.hpp>
#include <Color.hpp>
#include <Core/Name.hpp>
#include <Core/Types.hpp>
#include <Core/UndoStack.hpp>
#include <Material/MaterialID.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <Renderer/LightID.hpp>
#include <cstddef>

namespace fx {

class Object;
class LightBase;
class LightSpot;

namespace editor {

class EditorSelection;

struct EditOperationValue
{
	enum class eValueType
	{
		Vec3,
		Object,
		Light,
	} Type;

	union
	{
		Object* pObject;
		LightBase* pLight;
		Vec3f Position;
	};


	explicit EditOperationValue(const Vec3f& vec) : Type(eValueType::Vec3), Position(vec) {}
	explicit EditOperationValue(Object* obj) : Type(eValueType::Object), pObject(obj) {}
	explicit EditOperationValue(LightBase* light) : Type(eValueType::Light), pLight(light) {}

	explicit EditOperationValue(std::nullptr_t) : Type(eValueType::Object), pObject(nullptr) {}


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

	void Set(LightBase* light)
	{
		Type = eValueType::Light;
		pLight = light;
	}

	/// Same ambiguity as the constructor above, resolved the same way
	void Set(std::nullptr_t) { Set(static_cast<Object*>(nullptr)); }
};

struct EditOperation
{
	/// Mirrored by OPTYPE in `interop.strata`
	enum class eType
	{
		Move,
		Scale,
		Dupe,
		Create,
		Delete,
		Rotate,
		BrushEdit,
		CreateBrush,
		/// Moves and aims a spot light: `ValueA`/`ValueB` are its position before and after, and `Light` holds the rest
		LightTransform,
		/// Creates a spot light at `ValueA`'s position, aimed along `Light.DirectionAfter`
		LightCreate,
		/// Destroys a spot light; `LightSnap` holds everything needed to bring it back
		LightDelete,
	} Type;

public:
	/// Applies the operation. Objects that it destroys are removed from `selection` first.
	EditOperationValue Execute(EditorSelection& selection);

	/// Reverts the operation. Objects that it destroys are removed from `selection` first.
	void Undo(EditorSelection& selection);

	/// True if applying or reverting the operation creates or destroys objects
	bool ChangesObjects() const
	{
		return (Type == eType::Dupe || Type == eType::Create || Type == eType::CreateBrush || Type == eType::Delete);
	}

public:
	/// The object to manipulate
	Object* pObject = nullptr;

	ObjectID PushedObjectID = ObjectID::scNull;

	/// Stable ID for `ValueB` when it holds an object (Dupe result).
	ObjectID ValueObjectID = ObjectID::scNull;

	EditOperationValue ValueA;
	EditOperationValue ValueB;

	Brush::PlaneList PlanesBefore;
	Brush::PlaneList PlanesAfter;

	struct Snapshot
	{
		Vec3f Position = Vec3f::sZero;
		MaterialID Material = MaterialID::scNull;
		Quat Rotation = Quat::scIdentity;
		Name ObjectName;
		/// So that undoing the delete of a probe volume brush brings back a probe volume, not solid geometry
		bool bIsProbeVolume = false;
		bool bIsReflectionProbe = false;
		bool bIsDynamic = false;
	} ObjectSnapshot;

	struct LightEdit
	{
		LightSpot* pLight = nullptr;
		LightID Id = LightID::scNull;
		Vec3f DirectionBefore = Vec3f::sForward;
		Vec3f DirectionAfter = Vec3f::sForward;
	} Light;

	struct LightSnapshot
	{
		Name LightName;
		Vec3f Position = Vec3f::sZero;
		Vec3f Direction = Vec3f::sForward;
		float32 Radius = 5.0f;

		float32 InnerAngle = 0.0f;
		float32 OuterAngle = 0.0f;

		bool bCastShadows = true;

		Color Colour = Color::sWhite;
		float32 Intensity = 100000.0f;
	} LightSnap;

	/// The size of the operation group this is in. For example, when moving 10 objects, there will be 10 operations(one
	/// for each event) making the GroupSize = 10.
	int32 GroupSize = 1;
};


/**
 * @brief The undo/redo history of edit operations. Keeps the selection in step with objects that undoing and redoing
 * brings back or destroys.
 */
class EditHistory
{
	static constexpr uint32 scMaxOperations = 256;

public:
	explicit EditHistory(EditorSelection& selection);

	/// Records an operation and applies it
	EditOperationValue Push(const EditOperation& op);

	/// Reverts the last group of operations. Returns true if the selection changed.
	bool Undo();

	/// Reapplies the last undone group of operations. Returns true if the selection changed.
	bool Redo();

	void Clear();

	~EditHistory() = default;

private:
	EditorSelection& mSelection;
	UndoStack<EditOperation> mOperations;
};

} // namespace editor
} // namespace fx
