#include "EditOperation.hpp"

#include "EditorSelection.hpp"

#include <Blockout.hpp>
#include <Engine.hpp>
#include <Object/ObjectManager.hpp>
#include <World.hpp>

namespace fx::editor {

/// Returns the op's object only if it is still alive (guards against use-after-free
/// when an object was destroyed outside of undo/redo).
static Object* ResolveOpTarget(const EditOperation& op)
{
	if (op.pObject == nullptr || op.PushedObjectID.IsInvalid()) {
		return nullptr;
	}

	Object* live = gObjectManager->GetObject(op.PushedObjectID);
	return (live == op.pObject) ? live : nullptr;
}

/// Returns the op's `ValueB` object only if it is still alive (Dupe results).
static Object* ResolveOpValue(const EditOperation& op)
{
	if (op.ValueB.Type != EditOperationValue::eValueType::Object || op.ValueB.pObject == nullptr ||
		op.ValueObjectID.IsInvalid()) {
		return nullptr;
	}

	Object* live = gObjectManager->GetObject(op.ValueObjectID);
	return (live == op.ValueB.pObject) ? live : nullptr;
}

/////////////////////////////////////
// Edit Operations
/////////////////////////////////////

EditOperationValue EditOperation::Execute(EditorSelection& selection)
{
	switch (Type) {
	case eType::Move: {
		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);
		Assert(ValueB.Type == EditOperationValue::eValueType::Vec3);

		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		target->SetPosition(ValueB.Position);

		return ValueB;
	}
	case eType::Scale: {
		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);
		Assert(ValueB.Type == EditOperationValue::eValueType::Vec3);

		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		const Brush* brush = gWorld->pBlockout->GetBrush(target);
		if (brush == nullptr) {
			break;
		}

		PlanesBefore = brush->Planes;

		// ValueA is the face's normal and ValueB.X how far to move it
		gWorld->pBlockout->MoveFace(target, ValueA.Position, ValueB.Position.X);

		break;
	}
	case eType::Rotate: {
		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);
		Assert(ValueB.Type == EditOperationValue::eValueType::Vec3);

		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		target->SetRotation(Quat::FromEulerAngles(ValueB.Position));

		return ValueB;
	}
	case eType::BrushEdit: {
		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		gWorld->pBlockout->SetBrushPlanes(target, PlanesAfter);

		break;
	}
	case eType::Dupe: {
		// Origin point
		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);

		// Object to dupe
		Assert(pObject != nullptr);

		Vec3f origin = ValueA.Position;

		Object* dupe = gWorld->pBlockout->DupeObject(pObject);
		if (dupe == nullptr) {
			break;
		}

		dupe->SetPosition(origin);

		// DupeObject copies the source's *current* material, which is the selection
		// material while selected. Restore the original so dupes don't inherit it.
		dupe->SetMaterial(selection.GetStoredMaterial(pObject));

		ValueB.Set(dupe);
		ValueObjectID = dupe->ID;

		return ValueB;
	}
	case eType::Create: {
		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);

		pObject = (gWorld->pBlockout->NewObject(ValueA.Position));
		PushedObjectID = (pObject != nullptr) ? pObject->ID : ObjectID::scNull;

		return EditOperationValue(pObject);
	}
	case eType::CreateBrush: {
		pObject = gWorld->pBlockout->RestoreObject(ObjectSnapshot.Position, PlanesAfter, ObjectSnapshot.Material,
												   ObjectSnapshot.Rotation, ObjectSnapshot.ObjectName);
		PushedObjectID = (pObject != nullptr) ? pObject->ID : ObjectID::scNull;

		if (pObject != nullptr && ObjectSnapshot.bIsProbeVolume) {
			pObject->SetProbeVolume(true);
		}

		return EditOperationValue(pObject);
	}
	case eType::Delete: {
		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		selection.Remove(target);

		gWorld->pBlockout->DestroyObject(target);

		break;
	}
	}

	return ValueB;
}

void EditOperation::Undo(EditorSelection& selection)
{
	switch (Type) {
	case eType::Move: {
		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);
		Assert(ValueB.Type == EditOperationValue::eValueType::Vec3);

		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		target->SetPosition(ValueA.Position);

		break;
	}
	case eType::Scale: {
		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);
		Assert(ValueB.Type == EditOperationValue::eValueType::Vec3);

		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		// Restore the planes from before, as moving the face back could leave planes it dropped behind. The object is
		// put back where it was, as the brush is kept in place.
		gWorld->pBlockout->SetBrushPlanes(target, PlanesBefore);

		break;
	}
	case eType::Rotate: {
		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);
		Assert(ValueB.Type == EditOperationValue::eValueType::Vec3);

		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		target->SetRotation(Quat::FromEulerAngles(ValueA.Position));

		break;
	}
	case eType::BrushEdit: {
		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		gWorld->pBlockout->SetBrushPlanes(target, PlanesBefore);

		break;
	}
	case eType::Dupe: {
		Assert(pObject != nullptr);

		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);
		Assert(ValueB.Type == EditOperationValue::eValueType::Object);

		Object* dupe = ResolveOpValue(*this);
		if (dupe == nullptr) {
			break;
		}

		selection.Remove(dupe);

		gWorld->pBlockout->DestroyObject(dupe);

		ValueB.Set(nullptr);
		ValueObjectID = ObjectID::scNull;

		break;
	}
	case eType::Create: {
		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);

		Object* created = ResolveOpTarget(*this);
		if (created == nullptr) {
			break;
		}

		selection.Remove(created);

		gWorld->pBlockout->DestroyObject(created);

		pObject = nullptr;
		PushedObjectID = ObjectID::scNull;

		break;
	}
	case eType::CreateBrush: {
		Object* created = ResolveOpTarget(*this);
		if (created == nullptr) {
			break;
		}

		selection.Remove(created);

		gWorld->pBlockout->DestroyObject(created);

		pObject = nullptr;
		PushedObjectID = ObjectID::scNull;

		break;
	}
	case eType::Delete: {
		if (gWorld->pBlockout == nullptr) {
			break;
		}

		Object* restored = gWorld->pBlockout->RestoreObject(ObjectSnapshot.Position, PlanesBefore,
															ObjectSnapshot.Material, ObjectSnapshot.Rotation,
															ObjectSnapshot.ObjectName);

		if (restored == nullptr) {
			break;
		}

		if (ObjectSnapshot.bIsProbeVolume) {
			restored->SetProbeVolume(true);
		}

		pObject = restored;
		PushedObjectID = restored->ID;

		break;
	}
	}
}


/////////////////////////////////////
// Edit History
/////////////////////////////////////

EditHistory::EditHistory(EditorSelection& selection) : mSelection(selection)
{
	mOperations.InitCapacity(scMaxOperations);
}

EditOperationValue EditHistory::Push(const EditOperation& op)
{
	// Make room by dropping the oldest groups whole, so that no group is left half undoable
	const uint32 capacity = mOperations.GetCapacity();

	while (capacity > 0 && mOperations.GetSize() >= capacity) {
		const uint32 size = mOperations.GetSize();
		const uint32 evict = std::min(static_cast<uint32>(std::max(mOperations.First().GroupSize, 1)), size);

		for (uint32 i = 0; i < evict; i++) {
			mOperations.PopFront();
		}
	}

	EditOperation stamped = op;

	if (stamped.pObject != nullptr && stamped.PushedObjectID.IsInvalid()) {
		stamped.PushedObjectID = stamped.pObject->ID;
	}
	if (stamped.ValueB.Type == EditOperationValue::eValueType::Object && stamped.ValueB.pObject != nullptr &&
		stamped.ValueObjectID.IsInvalid()) {
		stamped.ValueObjectID = stamped.ValueB.pObject->ID;
	}

	mOperations.Push(stamped);

	EditOperation* pushed = mOperations.Last();
	Assert(pushed != nullptr);

	return pushed->Execute(mSelection);
}

bool EditHistory::Undo()
{
	EditOperation* first = mOperations.Last();
	if (first == nullptr) {
		return false;
	}

	// Clamp corrupt group sizes so one bad op can't unwind the whole stack.
	const int32 group_size = std::max(first->GroupSize, 1);

	bool selection_changed = false;

	// Pop the index first (matching Redo's DoRedo-then-apply order) so a failure
	// to apply can't desync the stack pointer from what was already mutated.
	for (int32 i = 0; i < group_size; i++) {
		EditOperation* op = mOperations.Last();
		if (op == nullptr) {
			break;
		}

		const EditOperation::eType type = op->Type;

		if (!mOperations.DoUndo()) {
			break;
		}

		op->Undo(mSelection);

		// Undoing a dupe selects the original again, and undoing a delete selects the object it brings back
		if (type == EditOperation::eType::Dupe) {
			Object* original = ResolveOpTarget(*op);
			if (original != nullptr) {
				mSelection.Add(original);
			}
		}
		else if (type == EditOperation::eType::Delete) {
			if (op->pObject != nullptr) {
				mSelection.Add(op->pObject);
			}
		}

		selection_changed |= op->ChangesObjects();
	}

	return selection_changed;
}

bool EditHistory::Redo()
{
	int32 group_size = 0;
	bool started = false;
	bool selection_changed = false;

	while (true) {
		EditOperation* op = mOperations.DoRedo();
		if (op == nullptr) {
			break;
		}

		if (!started) {
			group_size = std::max(op->GroupSize, 1);
			started = true;
		}

		const EditOperation::eType type = op->Type;

		const EditOperationValue result = op->Execute(mSelection);

		// Redoing a dupe swaps the selection from the original over to its dupe
		if (type == EditOperation::eType::Dupe) {
			mSelection.Remove(ResolveOpTarget(*op));
		}

		const bool creates_object = (type == EditOperation::eType::Dupe || type == EditOperation::eType::Create ||
									 type == EditOperation::eType::CreateBrush);

		if (creates_object && result.Type == EditOperationValue::eValueType::Object && result.pObject != nullptr) {
			mSelection.Add(result.pObject);
		}

		selection_changed |= op->ChangesObjects();

		if (--group_size <= 0) {
			break;
		}
	}

	return selection_changed;
}

void EditHistory::Clear() { mOperations.Clear(); }

} // namespace fx::editor
