#include "InGameEditor.hpp"

#include <Blockout.hpp>
#include <Controls.hpp>
#include <Engine.hpp>
#include <Object/ObjectManager.hpp>
#include <Script/ScriptManager.hpp>
#include <World.hpp>

namespace fx {

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

EditOperationValue EditOperation::Execute()
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

		ScaleBoundsMinBefore = target->Bounds.Min;
		ScaleBoundsMaxBefore = target->Bounds.Max;
		ScalePosBefore = target->GetPosition();

		gWorld->pBlockout->ScaleInDirection(target, ValueA.Position, ValueB.Position);
		gWorld->pBlockout->RebuildObject(target);

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
		if (gPrototypeEditor != nullptr) {
			dupe->SetMaterial(gPrototypeEditor->GetStoredMaterial(pObject));
		}

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
	case eType::Delete: {
		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		if (gPrototypeEditor != nullptr) {
			gPrototypeEditor->RemoveFromSelectionInternal(target);
		}

		gWorld->pBlockout->DestroyObject(target);

		break;
	}
	}

	return ValueB;
}

void EditOperation::Undo()
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

		// Restore the exact pre-scale state: re-applying a negated magnitude would
		// mis-split face travel between bounds and push-through shift.
		target->Bounds.Min = ScaleBoundsMinBefore;
		target->Bounds.Max = ScaleBoundsMaxBefore;
		target->SetPosition(ScalePosBefore);
		gWorld->pBlockout->RebuildObject(target);

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
	case eType::Dupe: {
		Assert(pObject != nullptr);

		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);
		Assert(ValueB.Type == EditOperationValue::eValueType::Object);

		Object* dupe = ResolveOpValue(*this);
		if (dupe == nullptr) {
			break;
		}

		if (gPrototypeEditor != nullptr) {
			gPrototypeEditor->RemoveFromSelectionInternal(dupe);
		}

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

		if (gPrototypeEditor != nullptr) {
			gPrototypeEditor->RemoveFromSelectionInternal(created);
		}

		gWorld->pBlockout->DestroyObject(created);

		pObject = nullptr;
		PushedObjectID = ObjectID::scNull;

		break;
	}
	case eType::Delete: {
		if (gWorld->pBlockout == nullptr) {
			break;
		}

		Object* restored = gWorld->pBlockout->RestoreObject(DeleteSnapshot.Position, DeleteSnapshot.BoundsMin,
															DeleteSnapshot.BoundsMax, DeleteSnapshot.Material,
															DeleteSnapshot.Rotation, DeleteSnapshot.ObjectName);

		if (restored == nullptr) {
			break;
		}

		if (DeleteSnapshot.bIsProbeVolume) {
			restored->SetProbeVolume(true);
		}

		pObject = restored;
		PushedObjectID = restored->ID;

		break;
	}
	}
}


/////////////////////////////////////
// Translate Object
/////////////////////////////////////

void EditorMode::Create(const String& name, const String& script_path)
{
	ModeName = name;
	pScript = gScriptManager->LoadScript(script_path.CStr());
	mOperationStack.InitCapacity(256);
}

void EditorMode::ReloadHotFunctions() { pUpdateFunction = pScript->GetFunction<UpdateFnDef>("mode_update"); }

void EditorMode::Update(const Vec3f& movement_vector, float32 delta_time)
{
	if (ControlManager::IsComboPressed(eKey::FX_KEY_LCTRL, eKey::FX_KEY_Z)) {
		if (ControlManager::IsKeyDown(eKey::FX_KEY_LSHIFT)) {
			Redo();
		}
		else {
			Undo();
		}
	}

	if (pUpdateFunction) {
		pUpdateFunction(movement_vector.mIntrin, delta_time);
	}
}

void EditorMode::Undo()
{
	EditOperation* first = mOperationStack.Last();
	if (first == nullptr) {
		return;
	}

	// Clamp corrupt group sizes so one bad op can't unwind the whole stack.
	int32 group_size = first->GroupSize;
	if (group_size < 1) {
		group_size = 1;
	}

	bool selection_changed = false;

	// Pop the index first (matching Redo's DoRedo-then-apply order) so a failure
	// to apply can't desync the stack pointer from what was already mutated.
	for (int i = 0; i < group_size; i++) {
		EditOperation* op = mOperationStack.Last();
		if (op == nullptr) {
			break;
		}

		EditOperation::eType type = op->Type;
		Object* undo_target = nullptr;

		// Capture pre-undo state needed for selection fixups.
		if (type == EditOperation::eType::Dupe) {
			undo_target = ResolveOpValue(*op);
		}
		else if (type == EditOperation::eType::Create) {
			undo_target = ResolveOpTarget(*op);
		}

		if (!mOperationStack.DoUndo()) {
			break;
		}

		op->Undo();

		// Selection fixups (C++-only; script is resynced once below).
		if (type == EditOperation::eType::Dupe) {
			if (undo_target != nullptr) {
				RemoveFromSelectionInternal(undo_target);
			}
			Object* original = (op->pObject != nullptr && !op->PushedObjectID.IsInvalid() &&
								gObjectManager->GetObject(op->PushedObjectID) == op->pObject)
								   ? op->pObject
								   : nullptr;
			if (original != nullptr) {
				AddToSelectionInternal(original);
			}
			selection_changed = true;
		}
		else if (type == EditOperation::eType::Create) {
			if (undo_target != nullptr) {
				RemoveFromSelectionInternal(undo_target);
			}
			selection_changed = true;
		}
		else if (type == EditOperation::eType::Delete) {
			if (op->pObject != nullptr) {
				AddToSelectionInternal(op->pObject);
			}
			selection_changed = true;
		}
	}

	if (selection_changed) {
		SyncScriptSelection();
	}
}


void EditorMode::Redo()
{
	// Peek group size without consuming: DoRedo advances the pointer, so read the
	// first redo slot's size via a speculative redo/undo round-trip is overkill;
	// instead consume one op at a time and follow its group count.
	bool started = false;
	int32 group_size = 0;
	bool selection_changed = false;

	while (true) {
		EditOperation* op = mOperationStack.DoRedo();
		if (op == nullptr) {
			break;
		}

		if (!started) {
			group_size = op->GroupSize;
			if (group_size < 1) {
				group_size = 1;
			}
			started = true;
		}

		EditOperation::eType type = op->Type;
		Object* redo_target = nullptr;
		if (type == EditOperation::eType::Delete) {
			redo_target = ResolveOpTarget(*op);
		}
		else if (type == EditOperation::eType::Dupe) {
			redo_target = ResolveOpTarget(*op);
		}

		EditOperationValue result = op->Execute();

		if (type == EditOperation::eType::Dupe) {
			if (redo_target != nullptr) {
				RemoveFromSelectionInternal(redo_target);
			}
			if (result.Type == EditOperationValue::eValueType::Object && result.pObject != nullptr) {
				AddToSelectionInternal(result.pObject);
			}
			selection_changed = true;
		}
		else if (type == EditOperation::eType::Create) {
			if (result.Type == EditOperationValue::eValueType::Object && result.pObject != nullptr) {
				AddToSelectionInternal(result.pObject);
			}
			selection_changed = true;
		}
		else if (type == EditOperation::eType::Delete) {
			if (redo_target != nullptr) {
				RemoveFromSelectionInternal(redo_target);
			}
			selection_changed = true;
		}

		if (--group_size <= 0) {
			break;
		}
	}

	if (selection_changed) {
		SyncScriptSelection();
	}
}


EditOperationValue EditorMode::PushEditOperation(const EditOperation& op)
{
	// Evict oldest *whole groups* first so Undo never observes a split group.
	// (UndoStack::Push would otherwise drop a single op off the front.)
	uint32 capacity = mOperationStack.GetCapacity();
	while (capacity > 0 && mOperationStack.GetSize() >= capacity) {
		EditOperation& oldest = mOperationStack.First();
		int32 evict = oldest.GroupSize;
		if (evict < 1) {
			evict = 1;
		}
		uint32 size = mOperationStack.GetSize();
		if ((uint32)evict > size) {
			evict = (int32)size;
		}
		for (int32 i = 0; i < evict; i++) {
			mOperationStack.PopFront();
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

	mOperationStack.Push(stamped);

	EditOperation* p_op = mOperationStack.Last();
	Assert(p_op != nullptr);

	return p_op->Execute();
}

MaterialID EditorMode::GetStoredMaterial(Object* object)
{
	if (object != nullptr) {
		for (SelectedObject& selected_obj : mSelectedObjects) {
			if (selected_obj.pObject == object) {
				return selected_obj.OldMaterial;
			}
		}
		return object->GetMaterialID();
	}

	return MaterialID::scNull;
}

void EditorMode::SetStoredMaterial(Object* object, MaterialID material)
{
	if (object == nullptr) {
		return;
	}

	for (SelectedObject& selected_obj : mSelectedObjects) {
		if (selected_obj.pObject == object) {
			selected_obj.OldMaterial = material;
			break;
		}
	}

	object->SetMaterial(material);
}

void EditorMode::AddToSelectionInternal(Object* object)
{
	if (object == nullptr || IsInSelection(object)) {
		return;
	}

	if (mSelectedObjects.Size >= scLimitSelectionObjects) {
		return;
	}

	mSelectedObjects.Insert(SelectedObject { .pObject = object, .OldMaterial = object->GetMaterialID() });
	object->SetMaterial(gWorld->pBlockout->SelectionMaterialID);
}

void EditorMode::RemoveFromSelectionInternal(Object* object)
{
	if (object == nullptr) {
		return;
	}

	for (uint32 i = 0; i < mSelectedObjects.Size; i++) {
		if (mSelectedObjects[i].pObject == object) {
			object->SetMaterial(mSelectedObjects[i].OldMaterial);
			mSelectedObjects[i] = mSelectedObjects[mSelectedObjects.Size - 1];
			mSelectedObjects.Size--;
			return;
		}
	}
}

void EditorMode::SyncScriptSelection()
{
	if (pScript == nullptr) {
		return;
	}

	auto mode_select_object = pScript->GetFunction<void (*)(void*, bool, bool)>("_internal_editor_select_object");
	if (!mode_select_object) {
		return;
	}

	mode_select_object(nullptr, false, false);
	for (SelectedObject& selected_obj : mSelectedObjects) {
		if (selected_obj.pObject == nullptr) {
			continue;
		}
		mode_select_object(reinterpret_cast<void*>(selected_obj.pObject), true, true);
	}
}


bool EditorMode::SelectObject(Object* object, bool append_selection)
{
	if (pScript == nullptr) {
		return false;
	}

	auto mode_select_object = pScript->GetFunction<void (*)(void*, bool, bool)>("_internal_editor_select_object");

	// Clear selection
	if (object == nullptr) {
		for (SelectedObject& selected_obj : mSelectedObjects) {
			if (selected_obj.pObject == nullptr) {
				continue;
			}

			selected_obj.pObject->SetMaterial(selected_obj.OldMaterial);
		}

		mSelectedObjects.Clear();

		if (mode_select_object) {
			mode_select_object(nullptr, false, false);
			return true;
		}

		return false;
	}

	if (object->HasTags(eObjectTag::LockTransform)) {
		return false;
	}

	if (IsInSelection(object)) {
		return true;
	}

	// If we aren't appending to the selection (overwrite selection), then we need to reset the previous
	// selection's materials
	if (append_selection == false) {
		for (SelectedObject& selected_obj : mSelectedObjects) {
			if (selected_obj.pObject == nullptr) {
				continue;
			}

			selected_obj.pObject->SetMaterial(selected_obj.OldMaterial);
		}
		mSelectedObjects.Clear();
	}

	mSelectedObjects.Insert(SelectedObject { .pObject = object, .OldMaterial = object->GetMaterialID() });

	// Set the newly selected object to the selection material.
	object->SetMaterial(gWorld->pBlockout->SelectionMaterialID);

	// Call the script's object selection routine
	if (mode_select_object) {
		mode_select_object(reinterpret_cast<void*>(object), true, append_selection);
		return true;
	}

	return false;
}

Object* EditorMode::GetLastSelectedObject()
{
	if (mSelectedObjects.Size == 0) {
		return nullptr;
	}

	return mSelectedObjects[mSelectedObjects.Size - 1].pObject;
}


bool EditorMode::IsInSelection(Object* object) const
{
	for (const SelectedObject& sel : mSelectedObjects) {
		if (sel.pObject == object) {
			return true;
		}
	}
	return false;
}


void EditorMode::Reload()
{
	auto mode_load = pScript->GetFunction<void (*)()>("mode_load");
	if (mode_load) {
		mode_load();
	}

	auto mode_set_xform_marker = pScript->GetFunction<void (*)(void*)>("editor_set_transform_marker");
	if (mode_set_xform_marker) {
		mode_set_xform_marker(reinterpret_cast<void*>(gWorld->pBlockout->pXFormObject));
	}

	ReloadHotFunctions();

	// Set selected objects back to selection material
	for (SelectedObject& selected_obj : mSelectedObjects) {
		if (selected_obj.pObject == nullptr) {
			continue;
		}

		selected_obj.pObject->SetMaterial(gWorld->pBlockout->SelectionMaterialID);
	}
}

void EditorMode::Unload()
{
	auto mode_unload = pScript->GetFunction<void (*)()>("mode_unload");
	if (mode_unload) {
		mode_unload();
	}

	// Reset selection materials
	if (mSelectedObjects.Size > 0) {
		for (SelectedObject& selected_obj : mSelectedObjects) {
			if (selected_obj.pObject == nullptr) {
				continue;
			}

			selected_obj.pObject->SetMaterial(selected_obj.OldMaterial);
		}
	}
}

void EditorMode::ResetUndoStack() { mOperationStack.Clear(); }

float EditorMode::GetQuantizeFraction() const
{
	auto editor_get_snap_value = pScript->GetFunction<float (*)()>("editor_get_snap_value");
	if (editor_get_snap_value) {
		return editor_get_snap_value();
	}

	return 0.0f;
}

bool EditorMode::GetQuantizeEnabled() const
{
	auto mode_get_quantize = pScript->GetFunction<bool (*)()>("editor_get_snap_enabled");
	if (mode_get_quantize) {
		return mode_get_quantize();
	}

	return false;
}


} // namespace fx
