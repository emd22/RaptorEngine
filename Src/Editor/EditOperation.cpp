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
#include <Script/ObjectScripts.hpp>
#include <World.hpp>
#include <algorithm>

namespace fx::editor {

static constexpr eObjectTag scEditableTags[] = {
	eObjectTag::LockTransform, eObjectTag::ProbeVolume, eObjectTag::ReflectionProbe,
	eObjectTag::Bleeds,		   eObjectTag::Spawn,		eObjectTag::Trigger,
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
	case eObjectTag::Spawn:
		return true;
	case eObjectTag::ProbeVolume:
	case eObjectTag::ReflectionProbe:
	case eObjectTag::Trigger:
		return object->HasTags(eObjectTag::Blockout);
	default:
		return false;
	}
}

bool CanAttachScript(const Object* object)
{
	if (object == nullptr || gWorld->pBlockout == nullptr) {
		return false;
	}

	return object->HasTags(eObjectTag::Blockout) || gWorld->pBlockout->IsModel(object);
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
		return !object->IsVolume();
	case eObjectFlags::PhysicsEnabled: {
		if (object->HasTags(eObjectTag::Blockout)) {
			return !object->IsVolume() && gWorld->pBlockout != nullptr && gWorld->pBlockout->HasBrush(object);
		}

		const physics::Body* body = gPhysics->GetBody(object->GetPhysicsID());

		return body != nullptr && body->mbHasPhysicsBody && body->GetMotionType() != physics::eMotionType::Static &&
			   !object->IsVolume();
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
	case eObjectTag::Trigger:
		object.SetTrigger(enabled);
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
		if (!object.HasTags(eObjectTag::Blockout) || gWorld->pBlockout == nullptr ||
			!gWorld->pBlockout->SetDynamic(&object, enabled)) {
			object.SetPhysicsEnabled(enabled);
		}
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
				if (flag == eObjectFlags::PhysicsEnabled && node == &root) {
					node->SetPosition(state.PositionBefore);
					node->SetRotation(state.RotationBefore);
				}

				SetObjectFlag(*node, flag, wanted, false);
			}
		}
	}
}

void EditOperation::StateEditData::CaptureBefore(Object& object)
{
	TagsBefore = static_cast<uint32>(object.Tags);
	PositionBefore = object.GetPosition();
	RotationBefore = object.mRotation;

	NodesBefore.clear();

	ForEachNode(object, [this](Object& node)
				{ NodesBefore.push_back(NodeFlags { .Id = node.ID, .Flags = static_cast<uint32>(node.GetFlags()) }); });
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

static Object* RestoreBrush(const EditOperation::Snapshot& snapshot, const Brush::PlaneList& planes)
{
	if (gWorld->pBlockout == nullptr) {
		return nullptr;
	}

	Object* object = gWorld->pBlockout->RestoreObject(snapshot.Position, planes, snapshot.Material, snapshot.Rotation,
													  snapshot.ObjectName, snapshot.bIsDynamic);

	if (object == nullptr) {
		return nullptr;
	}

	if (snapshot.bIsProbeVolume) {
		object->SetProbeVolume(true);
	}

	if (snapshot.bIsReflectionProbe) {
		object->SetReflectionProbe(true);
	}

	if (snapshot.bIsSpawn) {
		object->SetTag(eObjectTag::Spawn);
	}

	if (snapshot.bIsTrigger) {
		object->SetTrigger(true);
	}

	gObjectScripts->Attach(object, snapshot.ScriptPath);

	return object;
}

static void RestoreLight(EditOperation& op)
{
	const EditOperation::LightSnapshot& snapshot = op.LightSnap;

	Ref<LightSpot> light = gLightManager->NewLight<LightSpot>(snapshot.LightName.Get());

	light->SetPosition(snapshot.Position);
	light->SetDirection(snapshot.Direction);
	light->SetRadius(snapshot.Radius);
	light->SetConeAngles(snapshot.InnerAngle, snapshot.OuterAngle);
	light->bCastShadows = snapshot.bCastShadows;
	light->Color = snapshot.Colour;
	light->Intensity = snapshot.Intensity;

	op.Light.pLight = &(*light);
	op.Light.Id = light->ID;
}

static void DestroyLight(EditOperation& op)
{
	if (ResolveOpLight(op) != nullptr) {
		gLightManager->RemoveLight(op.Light.Id);
	}

	op.Light.pLight = nullptr;
}

EditOperation::Snapshot EditOperation::Snapshot::Capture(const Object& object, MaterialID material)
{
	return Snapshot {
		.Position = object.GetPosition(),
		.Material = material,
		.Rotation = object.mRotation,
		.ObjectName = object.Name,
		.bIsProbeVolume = object.IsProbeVolume(),
		.bIsReflectionProbe = object.IsReflectionProbe(),
		.bIsSpawn = object.IsSpawn(),
		.bIsTrigger = object.IsTrigger(),
		.bIsDynamic = gWorld->pBlockout->IsDynamic(&object),
		.ScriptPath = gObjectScripts->GetPath(object.ID),
	};
}

EditOperation::LightSnapshot EditOperation::LightSnapshot::Capture(const LightSpot& light)
{
	return LightSnapshot {
		.LightName = light.Name,
		.Position = light.GetPosition(),
		.Direction = light.GetDirection().Normalize(),
		.Radius = light.GetRadius(),
		.InnerAngle = light.GetInnerAngle(),
		.OuterAngle = light.GetOuterAngle(),
		.bCastShadows = light.bCastShadows,
		.Colour = light.Color,
		.Intensity = light.Intensity,
	};
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
	case eType::ScriptEdit: {
		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		gObjectScripts->Attach(target, ScriptEdit.After);

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
	case eType::SpawnTransform: {
		gWorld->PlayerSpawn.Position = ValueB.Position;
		gWorld->PlayerSpawn.Direction = Spawn.DirectionAfter;
		gWorld->PlayerSpawn.bCustom = Spawn.bCustomAfter;

		return ValueB;
	}
	case eType::LightCreate: {
		RestoreLight(*this);

		return EditOperationValue(static_cast<LightBase*>(Light.pLight));
	}
	case eType::LightDelete: {
		DestroyLight(*this);

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
			ValueB.Set(nullptr);
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
		pObject = RestoreBrush(ObjectSnapshot, PlanesAfter);
		PushedObjectID = (pObject != nullptr) ? pObject->ID : ObjectID::scNull;

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
	case eType::ScriptEdit: {
		Object* target = ResolveOpTarget(*this);
		if (target == nullptr) {
			break;
		}

		gObjectScripts->Attach(target, ScriptEdit.Before);

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
	case eType::SpawnTransform: {
		gWorld->PlayerSpawn.Position = ValueA.Position;
		gWorld->PlayerSpawn.Direction = Spawn.DirectionBefore;
		gWorld->PlayerSpawn.bCustom = Spawn.bCustomBefore;

		break;
	}
	case eType::LightCreate: {
		DestroyLight(*this);

		break;
	}
	case eType::LightDelete: {
		RestoreLight(*this);

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
	case eType::Create:
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
		Object* restored = RestoreBrush(ObjectSnapshot, PlanesBefore);
		if (restored == nullptr) {
			break;
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

	return mOperations.Push(std::move(stamped)).Execute(mSelection);
}

bool EditHistory::Undo()
{
	bool selection_changed = false;

	// Every operation in a group carries the size of the group. It is clamped so that one bad operation can't unwind
	// the whole stack.
	int32 remaining = 1;

	for (bool first = true; remaining > 0; remaining--, first = false) {
		EditOperation* op = mOperations.Undo();
		if (op == nullptr) {
			break;
		}

		if (first) {
			remaining = std::max(op->GroupSize, 1);
		}

		op->Undo(mSelection);

		// Undoing a dupe selects the original again, and undoing a delete selects the object it brings back
		if (op->Type == EditOperation::eType::Dupe) {
			Object* original = ResolveOpTarget(*op);
			if (original != nullptr) {
				mSelection.Add(original);
			}
		}
		else if (op->Type == EditOperation::eType::Delete && op->pObject != nullptr) {
			mSelection.Add(op->pObject);
		}

		selection_changed |= op->ChangesObjects();
	}

	return selection_changed;
}

bool EditHistory::Redo()
{
	bool selection_changed = false;

	int32 remaining = 1;

	for (bool first = true; remaining > 0; remaining--, first = false) {
		EditOperation* op = mOperations.Redo();
		if (op == nullptr) {
			break;
		}

		if (first) {
			remaining = std::max(op->GroupSize, 1);
		}

		const EditOperationValue result = op->Execute(mSelection);

		// Redoing a dupe swaps the selection from the original over to its dupe
		if (op->Type == EditOperation::eType::Dupe) {
			mSelection.Remove(ResolveOpTarget(*op));
		}

		if (op->CreatesObject() && result.Type == EditOperationValue::eValueType::Object && result.pObject != nullptr) {
			mSelection.Add(result.pObject);
		}

		selection_changed |= op->ChangesObjects();
	}

	return selection_changed;
}

void EditHistory::Clear() { mOperations.Clear(); }

} // namespace fx::editor
