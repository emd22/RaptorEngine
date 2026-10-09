#pragma once

#include <Brush.hpp>
#include <Color.hpp>
#include <Core/Name.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Core/UndoStack.hpp>
#include <Material/MaterialID.hpp>
#include <Math/BBox.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <Renderer/LightID.hpp>
#include <memory>
#include <vector>

namespace fx {

class Object;
class LightSpot;

namespace editor {

class EditorSelection;

/////////////////////////////////////
// Base classes
/////////////////////////////////////

class EditOperation
{
public:
	explicit EditOperation(int32 group_size = 1) : GroupSize(group_size) {}
	virtual ~EditOperation() = default;

	/// Applies the operation. Objects that it destroys are removed from `selection` first.
	virtual void Execute(EditorSelection& selection) = 0;

	/// Reverts the operation. Objects that it destroys are removed from `selection` first.
	virtual void Undo(EditorSelection& selection) = 0;

	virtual void Redo(EditorSelection& selection) { Execute(selection); }

	/// True if applying or reverting the operation creates or destroys objects
	virtual bool ChangesObjects() const { return false; }

public:
	/// The size of the operation group this is in. For example, when moving 10 objects, there will be 10 operations(one
	/// for each event) making the GroupSize = 10.
	int32 GroupSize;
};

class ObjectEditOperation : public EditOperation
{
public:
	void Retarget(Object* object);

protected:
	ObjectEditOperation(Object* object, int32 group_size);

	Object* ResolveTarget() const;

private:
	Object* mpObject = nullptr;
	ObjectID mObjectID = ObjectID::scNull;
};

/////////////////////////////////////
// Block Creation
/////////////////////////////////////
class ObjectCreationOperation : public ObjectEditOperation
{
public:
	void Undo(EditorSelection& selection) override;
	void Redo(EditorSelection& selection) override;

	bool ChangesObjects() const override { return true; }

	Object* GetCreated() const { return ResolveTarget(); }

protected:
	explicit ObjectCreationOperation(int32 group_size) : ObjectEditOperation(nullptr, group_size) {}
};

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
};

/////////////////////////////////////
// Light Edits
/////////////////////////////////////
class LightEditOperation : public EditOperation
{
protected:
	LightEditOperation(LightSpot* light, int32 group_size);

	LightSpot* ResolveLight() const;

	void Restore(const LightSnapshot& snapshot);
	void Destroy();

protected:
	LightSpot* mpLight = nullptr;
	LightID mLightID = LightID::scNull;
};

struct ObjectSnapshot
{
	Vec3f Position = Vec3f::sZero;
	MaterialID Material = MaterialID::scNull;
	Quat Rotation = Quat::scIdentity;
	Name ObjectName;
	/// So that undoing the delete of a probe volume brush brings back a probe volume, not solid geometry
	bool bIsProbeVolume = false;
	bool bIsReflectionProbe = false;
	bool bIsSpawn = false;
	bool bIsTrigger = false;
	bool bIsDynamic = false;
	String ScriptPath;
	Vec3f EnterDirection = Vec3f::sZero;

	/// Records everything needed to bring `object` back. `material` is passed in as a selected object wears the
	/// selection material instead of its own.
	static ObjectSnapshot Capture(const Object& object, MaterialID material);
};

bool CanEditObjectTag(const Object* object, uint32 tag_bit);
bool CanEditObjectFlag(const Object* object, uint32 flag_bit);

bool CanAttachScript(const Object* object);

/////////////////////////////////////
// Object operations
/////////////////////////////////////

class MoveOperation final : public ObjectEditOperation
{
public:
	MoveOperation(Object* object, const Vec3f& before, const Vec3f& after, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	Vec3f mBefore;
	Vec3f mAfter;
};

class RotateOperation final : public ObjectEditOperation
{
public:
	RotateOperation(Object* object, const Vec3f& euler_before, const Vec3f& euler_after, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	Vec3f mEulerBefore;
	Vec3f mEulerAfter;
};

class ScaleFaceOperation final : public ObjectEditOperation
{
public:
	ScaleFaceOperation(Object* object, const Vec3f& face_normal, float32 distance, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	Vec3f mFaceNormal;
	float32 mDistance;
	Brush::PlaneList mPlanesBefore;
};

/////////////////////////////////////
// Brush Operations
/////////////////////////////////////

class BrushEditOperation final : public ObjectEditOperation
{
public:
	BrushEditOperation(Object* object, const Brush::PlaneList& before, const Brush::PlaneList& after,
					   int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	Brush::PlaneList mPlanesBefore;
	Brush::PlaneList mPlanesAfter;
};

/// Resizes an object's bounding box
class BoundsEditOperation final : public ObjectEditOperation
{
public:
	BoundsEditOperation(Object* object, const BBox& before, const BBox& after, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	BBox mBoundsBefore;
	BBox mBoundsAfter;
};

/// Toggles one of an object's tags or flags
class ObjectStateEditOperation final : public ObjectEditOperation
{
public:
	ObjectStateEditOperation(Object* object, uint32 bit, bool is_tag, bool enabled, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	struct NodeFlags
	{
		ObjectID Id = ObjectID::scNull;
		uint32 Flags = 0;
	};

private:
	void RestoreState(Object& root) const;

private:
	uint32 mBit;
	bool mbIsTag;
	bool mbEnabled;

	uint32 mTagsBefore = 0;
	Vec3f mPositionBefore = Vec3f::sZero;
	Quat mRotationBefore = Quat::scIdentity;
	std::vector<NodeFlags> mNodesBefore;
};

class ScriptEditOperation final : public ObjectEditOperation
{
public:
	ScriptEditOperation(Object* object, const String& before, const String& after, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	String mBefore;
	String mAfter;
};

class TriggerDirectionEditOperation final : public ObjectEditOperation
{
public:
	TriggerDirectionEditOperation(Object* object, const Vec3f& before, const Vec3f& after, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	Vec3f mBefore;
	Vec3f mAfter;
};

/////////////////////////////////////
// Object creation and deletion
/////////////////////////////////////

class CreateOperation final : public ObjectCreationOperation
{
public:
	explicit CreateOperation(const Vec3f& position, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;

private:
	Vec3f mPosition;
};

class CreateBrushOperation final : public ObjectCreationOperation
{
public:
	CreateBrushOperation(const Brush::PlaneList& planes, const ObjectSnapshot& snapshot, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;

private:
	Brush::PlaneList mPlanes;
	ObjectSnapshot mSnapshot;
};

class DupeOperation final : public ObjectEditOperation
{
public:
	DupeOperation(Object* original, const Vec3f& position, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;
	void Redo(EditorSelection& selection) override;

	bool ChangesObjects() const override { return true; }

	Object* GetDupe() const;

private:
	Vec3f mPosition;
	Object* mpDupe = nullptr;
	ObjectID mDupeID = ObjectID::scNull;
};

class DeleteOperation final : public ObjectEditOperation
{
public:
	DeleteOperation(Object* object, const Brush::PlaneList& planes, const ObjectSnapshot& snapshot,
					int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

	bool ChangesObjects() const override { return true; }

private:
	Brush::PlaneList mPlanes;
	ObjectSnapshot mSnapshot;
};

/////////////////////////////////////
// Light operations
/////////////////////////////////////

/// Moves and aims a spot light
class LightTransformOperation final : public LightEditOperation
{
public:
	LightTransformOperation(LightSpot* light, const Vec3f& position_before, const Vec3f& position_after,
							const Vec3f& direction_before, const Vec3f& direction_after);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	Vec3f mPositionBefore;
	Vec3f mPositionAfter;
	Vec3f mDirectionBefore;
	Vec3f mDirectionAfter;
};

class LightCreateOperation final : public LightEditOperation
{
public:
	explicit LightCreateOperation(const LightSnapshot& snapshot);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

	LightSpot* GetCreated() const { return ResolveLight(); }

private:
	LightSnapshot mSnapshot;
};

class LightDeleteOperation final : public LightEditOperation
{
public:
	explicit LightDeleteOperation(LightSpot* light);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	LightSnapshot mSnapshot;
};

/////////////////////////////////////
// Player spawn
/////////////////////////////////////

/// Moves and aims the player spawn
class SpawnTransformOperation final : public EditOperation
{
public:
	struct State
	{
		Vec3f Position = Vec3f::sZero;
		Vec3f Direction = Vec3f::sForward;
		bool bCustom = false;
	};

public:
	SpawnTransformOperation(const State& before, const State& after);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	State mBefore;
	State mAfter;
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

	EditOperation& Push(std::unique_ptr<EditOperation> op);

	/// Reverts the last group of operations. Returns true if the selection changed.
	bool Undo();

	/// Reapplies the last undone group of operations. Returns true if the selection changed.
	bool Redo();

	void Clear();

	~EditHistory() = default;

private:
	EditorSelection& mSelection;
	UndoStack<std::unique_ptr<EditOperation>> mOperations;
};

} // namespace editor
} // namespace fx
