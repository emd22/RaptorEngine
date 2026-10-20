#pragma once

#include "BaseOperation.hpp"

#include <Brush.hpp>
#include <Core/DynArray.hpp>
#include <Object/ObjectFlags.hpp>

namespace fx {


namespace editor {

/**
 * @brief The basis for all object/brush creation operations.
 */
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


/////////////////////////////////////
// Basic Tranform operations
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
	RotateOperation(Object* object, const Quat& before, const Quat& after, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	Quat mBefore;
	Quat mAfter;
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

class PatchEditOperation final : public ObjectEditOperation
{
public:
	PatchEditOperation(Object* object, Patch&& before, Patch&& after, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	std::shared_ptr<const Patch> mpBefore;
	std::shared_ptr<const Patch> mpAfter;
};

/// Toggles one of an object's tags or flags
class ObjectStateEditOperation final : public ObjectEditOperation
{
public:
	ObjectStateEditOperation(Object* object, eObjectTag tag, bool enabled, int32 group_size = 1);
	ObjectStateEditOperation(Object* object, eObjectFlags flag, bool enabled, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;

private:
	struct NodeFlags
	{
		ObjectID Id = ObjectID::scNull;
		eObjectFlags Flags = eObjectFlags::None;
	};

private:
	void CaptureState(Object& object);
	void RestoreState(Object& root) const;

private:
	eObjectTag mTag = eObjectTag::None;
	eObjectFlags mFlag = eObjectFlags::None;
	bool mbEnabled;

	eObjectTag mTagsBefore = eObjectTag::None;
	Vec3f mPositionBefore = Vec3f::sZero;
	Quat mRotationBefore = Quat::scIdentity;
	DynArray<NodeFlags> mNodesBefore;
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

class RenameEditOperation final : public ObjectEditOperation
{
public:
	RenameEditOperation(Object* object, const String& before, const String& after, int32 group_size = 1);

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
	explicit CreateOperation(const Vec3f& position, int32 group_size = 1, MaterialID material = MaterialID::scNull);

	void Execute(EditorSelection& selection) override;

private:
	Vec3f mPosition;
	MaterialID mMaterial;
};

/**
 * @brief Creates a new brish from the given planes.
 */
class CreateBrushOperation final : public ObjectCreationOperation
{
public:
	CreateBrushOperation(const Brush::PlaneList& planes, const ObjectSnapshot& snapshot, int32 group_size = 1);

	void Execute(EditorSelection& selection) override;

private:
	Brush::PlaneList mPlanes;
	ObjectSnapshot mSnapshot;
};

/**
 * @brief Duplicates all items in a selection.
 */
class DupeOperation final : public ObjectEditOperation
{
public:
	/// `dupe_subview` is the subview instance that the dupe becomes, when a placed subview is being copied
	DupeOperation(Object* original, const Vec3f& position, int32 group_size = 1, uint32 dupe_subview = 0);

	void Execute(EditorSelection& selection) override;
	void Undo(EditorSelection& selection) override;
	void Redo(EditorSelection& selection) override;

	bool ChangesObjects() const override { return true; }

	Object* GetDupe() const;

private:
	Vec3f mPosition;
	uint32 mDupeSubview = 0;
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
// Object Functions
/////////////////////////////////////

bool CanEditObjectTag(const Object* object, eObjectTag tag);
bool CanEditObjectFlag(const Object* object, eObjectFlags flag);

eObjectTag GetEditableTags(const Object* object);
eObjectFlags GetEditableFlags(const Object* object);

bool CanAttachScript(const Object* object);


} // namespace editor

} // namespace fx
