#include "EditOperation.hpp"

#include "EditorSelection.hpp"

#include <Blockout.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/LightManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <World.hpp>

namespace fx::editor {

static constexpr eObjectTag scEditableTags[] = {
	eObjectTag::LockTransform,
	eObjectTag::ProbeVolume,
	eObjectTag::ReflectionProbe,
	eObjectTag::Bleeds,
};

static constexpr eObjectFlags scEditableFlags[] = {
	eObjectFlags::PhysicsEnabled,
	eObjectFlags::ShadowCaster,
	eObjectFlags::DisableCulling,
	eObjectFlags::NotProbeVisible,
};

bool CanEditObjectTag(const Object* object, uint32 tag_bit)
{
	if (object == nullptr) {
		return false;
	}

	switch (static_cast<eObjectTag>(tag_bit)) {
	case eObjectTag::LockTransform:
	case eObjectTag::Bleeds:
		return true;
	case eObjectTag::ProbeVolume:
	case eObjectTag::ReflectionProbe:
		return object->HasTags(eObjectTag::Blockout);
	default:
		return false;
	}
}

bool CanEditObjectFlag(const Object* object, uint32 flag_bit)
{
	if (object == nullptr) {
		return false;
	}

	switch (static_cast<eObjectFlags>(flag_bit)) {
	case eObjectFlags::DisableCulling:
		return true;
	case eObjectFlags::ShadowCaster:
	case eObjectFlags::NotProbeVisible:
		return !object->IsProbeVolume();
	case eObjectFlags::PhysicsEnabled: {
		const physics::Body* body = gPhysics->GetBody(object->GetPhysicsID());

		return body != nullptr && body->mbHasPhysicsBody && body->GetMotionType() != physics::eMotionType::Static &&
			   !object->IsProbeVolume();
	}
	default:
		return false;
	}
}

template <typename TFunc>
static void ForEachNode(Object& object, const TFunc& func)
{
	func(object);

	for (ObjectID attached_id : object.AttachedNodes) {
		Object* attached = gObjectManager->GetObject(attached_id);

		if (attached != nullptr) {
			ForEachNode(*attached, func);
		}
	}
}

static void SetObjectTag(Object& object, eObjectTag tag, bool enabled)
{
	switch (tag) {
	case eObjectTag::ProbeVolume:
		object.SetProbeVolume(enabled);
		break;
	case eObjectTag::ReflectionProbe:
		if (enabled) {
			object.SetReflectionProbe(true);
		}
		else {
			object.ClearTag(eObjectTag::ReflectionProbe);
		}
		break;
	default:
		if (enabled) {
			object.SetTag(tag);
		}
		else {
			object.ClearTag(tag);
		}
		break;
	}
}

static void SetObjectFlag(Object& object, eObjectFlags flag, bool enabled, bool recursive)
{
	switch (flag) {
	case eObjectFlags::PhysicsEnabled:
		object.SetPhysicsEnabled(enabled);
		break;
	case eObjectFlags::ShadowCaster:
		if (recursive) {
			ForEachNode(object, [enabled](Object& node) { node.SetShadowCaster(enabled); });
		}
		else {
			object.SetShadowCaster(enabled);
		}
		break;
	case eObjectFlags::DisableCulling:
		if (recursive) {
			ForEachNode(object, [enabled](Object& node) { node.SetCullable(!enabled); });
		}
		else {
			object.SetCullable(!enabled);
		}
		break;
	case eObjectFlags::NotProbeVisible:
		object.SetProbeVisible(!enabled);
		break;
	default:
		break;
	}
}

static void ApplyTag(Object& object, uint32 tag_bit, bool enabled)
{
	const eObjectTag tag = static_cast<eObjectTag>(tag_bit);

	if (!CanEditObjectTag(&object, tag_bit) || object.HasTags(tag) == enabled) {
		return;
	}

	const bool was_reflection_probe = object.IsReflectionProbe();

	SetObjectTag(object, tag, enabled);

	if (was_reflection_probe != object.IsReflectionProbe()) {
		gProbeManager->RebuildReflectionProbesFromWorld();
	}
}

static void ApplyFlag(Object& object, uint32 flag_bit, bool enabled)
{
	const eObjectFlags flag = static_cast<eObjectFlags>(flag_bit);

	if (!CanEditObjectFlag(&object, flag_bit) || HasFlag(object.GetFlags(), flag) == enabled) {
		return;
	}

	SetObjectFlag(object, flag, enabled, true);
}

static void RestoreObjectState(Object& root, const EditOperation::StateEditData& state)
{
	for (const eObjectTag tag : scEditableTags) {
		const uint32 tag_bit = static_cast<uint32>(tag);

		ApplyTag(root, tag_bit, (state.TagsBefore & tag_bit) != 0);
	}

	for (const EditOperation::NodeFlags& saved : state.NodesBefore) {
		Object* node = gObjectManager->GetObject(saved.Id);

		if (node == nullptr) {
			continue;
		}

		for (const eObjectFlags flag : scEditableFlags) {
			const uint32 flag_bit = static_cast<uint32>(flag);
			const bool wanted = (saved.Flags & flag_bit) != 0;

			if (CanEditObjectFlag(node, flag_bit) && HasFlag(node->GetFlags(), flag) != wanted) {
				SetObjectFlag(*node, flag, wanted, false);
			}
		}
	}
}

void EditOperation::StateEditData::CaptureBefore(Object& object)
{
	TagsBefore = static_cast<uint32>(object.Tags);

	NodesBefore.clear();

	ForEachNode(object, [this](Object& node) {
		NodesBefore.push_back(NodeFlags { .Id = node.ID, .Flags = static_cast<uint32>(node.GetFlags()) });
	});
}

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


static LightSpot* ResolveOpLight(const EditOperation& op)
{
	if (op.Light.pLight == nullptr || op.Light.Id.IsInvalid()) {
		return nullptr;
	}

	LightBase* live = gLightManager->GetLight(op.Light.Id);
	return (live == op.Light.pLight) ? op.Light.pLight : nullptr;
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
	case eType::BoundsEdit: {
		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		target->SetBounds(BoundsAfter);

		break;
	}
	case eType::ObjectStateEdit: {
		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		if (StateEdit.bIsTag) {
			ApplyTag(*target, StateEdit.Bit, StateEdit.bEnabled);
		}
		else {
			ApplyFlag(*target, StateEdit.Bit, StateEdit.bEnabled);
		}

		break;
	}
	case eType::LightTransform: {
		LightSpot* light = ResolveOpLight(*this);
		if (light == nullptr) {
			break;
		}

		light->SetPosition(ValueB.Position);
		light->SetDirection(Light.DirectionAfter);

		return ValueB;
	}
	case eType::LightCreate: {
		Assert(ValueA.Type == EditOperationValue::eValueType::Vec3);

		Ref<LightSpot> light = gLightManager->NewLight<LightSpot>(LightSnap.LightName.Get());
		light->SetPosition(ValueA.Position);
		light->SetDirection(Light.DirectionAfter);
		light->SetRadius(LightSnap.Radius);
		light->SetConeAngles(LightSnap.InnerAngle, LightSnap.OuterAngle);
		light->bCastShadows = LightSnap.bCastShadows;
		light->Color = LightSnap.Colour;
		light->Intensity = LightSnap.Intensity;

		Light.pLight = &(*light);
		Light.Id = light->ID;

		return EditOperationValue(static_cast<LightBase*>(Light.pLight));
	}
	case eType::LightDelete: {
		LightSpot* light = ResolveOpLight(*this);
		if (light == nullptr) {
			break;
		}

		gLightManager->DestroyLight(Light.Id);
		Light.pLight = nullptr;

		break;
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
		pObject = gWorld->pBlockout->RestoreObject(
			ObjectSnapshot.Position, PlanesAfter, ObjectSnapshot.Material, ObjectSnapshot.Rotation,
			ObjectSnapshot.ObjectName, ObjectSnapshot.bIsDynamic);
		PushedObjectID = (pObject != nullptr) ? pObject->ID : ObjectID::scNull;

		if (pObject != nullptr && ObjectSnapshot.bIsProbeVolume) {
			pObject->SetProbeVolume(true);
		}

		if (pObject != nullptr && ObjectSnapshot.bIsReflectionProbe) {
			pObject->SetReflectionProbe(true);
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
	case eType::BoundsEdit: {
		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		target->SetBounds(BoundsBefore);

		break;
	}
	case eType::ObjectStateEdit: {
		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		RestoreObjectState(*target, StateEdit);

		break;
	}
	case eType::LightTransform: {
		LightSpot* light = ResolveOpLight(*this);
		if (light == nullptr) {
			break;
		}

		light->SetPosition(ValueA.Position);
		light->SetDirection(Light.DirectionBefore);

		break;
	}
	case eType::LightCreate: {
		if (Light.Id.IsInvalid()) {
			break;
		}

		gLightManager->DestroyLight(Light.Id);
		Light.pLight = nullptr;

		break;
	}
	case eType::LightDelete: {
		Ref<LightSpot> restored = gLightManager->NewLight<LightSpot>(LightSnap.LightName.Get());

		restored->SetPosition(LightSnap.Position);
		restored->SetDirection(LightSnap.Direction);
		restored->SetRadius(LightSnap.Radius);
		restored->SetConeAngles(LightSnap.InnerAngle, LightSnap.OuterAngle);
		restored->bCastShadows = LightSnap.bCastShadows;
		restored->Color = LightSnap.Colour;
		restored->Intensity = LightSnap.Intensity;

		Light.pLight = &(*restored);
		Light.Id = restored->ID;

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

		Object* restored = gWorld->pBlockout->RestoreObject(
			ObjectSnapshot.Position, PlanesBefore, ObjectSnapshot.Material, ObjectSnapshot.Rotation,
			ObjectSnapshot.ObjectName, ObjectSnapshot.bIsDynamic);

		if (restored == nullptr) {
			break;
		}

		if (ObjectSnapshot.bIsProbeVolume) {
			restored->SetProbeVolume(true);
		}

		if (ObjectSnapshot.bIsReflectionProbe) {
			restored->SetReflectionProbe(true);
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
	if (stamped.Light.pLight != nullptr && stamped.Light.Id.IsInvalid()) {
		stamped.Light.Id = stamped.Light.pLight->ID;
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
