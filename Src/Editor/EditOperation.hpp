#pragma once

#include <Brush.hpp>
#include <Color.hpp>
#include <Core/Name.hpp>
#include <Core/Types.hpp>
#include <Core/UndoStack.hpp>
#include <Material/MaterialID.hpp>
#include <Math/BoundingBox.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <Renderer/LightID.hpp>
#include <cstddef>
#include <vector>

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

	EditOperationValue() : Type(eValueType::Vec3), Position(Vec3f::sZero) {}

	explicit EditOperationValue(const Vec3f vec) : Type(eValueType::Vec3), Position(vec) {}
	explicit EditOperationValue(Object* obj) : Type(eValueType::Object), pObject(obj) {}
	explicit EditOperationValue(LightBase* light) : Type(eValueType::Light), pLight(light) {}

	explicit EditOperationValue(std::nullptr_t) : Type(eValueType::Object), pObject(nullptr) {}

	void Set(const Vec3f vec)
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
		/// Creates a spot light from `LightSnap`
		LightCreate,
		/// Destroys a spot light; `LightSnap` holds everything needed to bring it back
		LightDelete,
		/// Resizes an object's bounding box: `BoundsBefore` and `BoundsAfter` hold the box on either side of the edit
		BoundsEdit,
		/// Toggles one of an object's tags or flags; `StateEdit` holds the bit and the state to go back to
		ObjectStateEdit,
		/// Moves and aims the player spawn: `ValueA`/`ValueB` are its position before and after, and `Spawn` holds the
		/// rest
		SpawnTransform,
	} Type;

public:
	/// Applies the operation. Objects that it destroys are removed from `selection` first.
	EditOperationValue Execute(EditorSelection& selection);

	/// Reverts the operation. Objects that it destroys are removed from `selection` first.
	void Undo(EditorSelection& selection);

	/// True if applying the operation creates an object
	bool CreatesObject() const { return (Type == eType::Dupe || Type == eType::Create || Type == eType::CreateBrush); }

	/// True if applying or reverting the operation creates or destroys objects
	bool ChangesObjects() const { return (CreatesObject() || Type == eType::Delete); }

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

	AABB BoundsBefore;
	AABB BoundsAfter;

	struct Snapshot
	{
		Vec3f Position = Vec3f::sZero;
		MaterialID Material = MaterialID::scNull;
		Quat Rotation = Quat::scIdentity;
		Name ObjectName;
		/// So that undoing the delete of a probe volume brush brings back a probe volume, not solid geometry
		bool bIsProbeVolume = false;
		bool bIsReflectionProbe = false;
		bool bIsSpawn = false;
		bool bIsDynamic = false;

		/// Records everything needed to bring `object` back. `material` is passed in as a selected object wears the
		/// selection material instead of its own.
		static Snapshot Capture(const Object& object, MaterialID material);
	} ObjectSnapshot;

	struct NodeFlags
	{
		ObjectID Id = ObjectID::scNull;
		uint32 Flags = 0;
	};

	struct StateEditData
	{
		uint32 Bit = 0;
		bool bIsTag = false;
		bool bEnabled = false;
		uint32 TagsBefore = 0;
		Vec3f PositionBefore = Vec3f::sZero;
		Quat RotationBefore = Quat::scIdentity;
		std::vector<NodeFlags> NodesBefore;

		void CaptureBefore(Object& object);
	} StateEdit;

	struct SpawnEdit
	{
		Vec3f DirectionBefore = Vec3f::sForward;
		Vec3f DirectionAfter = Vec3f::sForward;
		bool bCustomBefore = false;
		bool bCustomAfter = true;
	} Spawn;

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

		static LightSnapshot Capture(const LightSpot& light);
	} LightSnap;

	/// The size of the operation group this is in. For example, when moving 10 objects, there will be 10 operations(one
	/// for each event) making the GroupSize = 10.
	int32 GroupSize = 1;
};


bool CanEditObjectTag(const Object* object, uint32 tag_bit);
bool CanEditObjectFlag(const Object* object, uint32 flag_bit);

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
