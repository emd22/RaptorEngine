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

static Object* RestoreBrush(const ObjectSnapshot& snapshot, const Brush::PlaneList& planes)
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
	gObjectScripts->SetRequiredEnterDirection(object->ID, snapshot.EnterDirection);

	return object;
}

ObjectSnapshot ObjectSnapshot::Capture(const Object& object, MaterialID material)
{
	Vec3f enter_direction = Vec3f::sZero;
	gObjectScripts->TryGetRequiredEnterDirection(object.ID, enter_direction);

	return ObjectSnapshot {
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
		.EnterDirection = enter_direction,
	};
}

LightSnapshot LightSnapshot::Capture(const LightSpot& light)
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
// Base classes
/////////////////////////////////////

ObjectEditOperation::ObjectEditOperation(Object* object, int32 group_size) : EditOperation(group_size)
{
	Retarget(object);
}

void ObjectEditOperation::Retarget(Object* object)
{
	mpObject = object;
	mObjectID = (object != nullptr) ? object->ID : ObjectID::scNull;
}

/// Returns the op's object only if it is still alive (guards against use-after-free
/// when an object was destroyed outside of undo/redo).
Object* ObjectEditOperation::ResolveTarget() const
{
	if (mpObject == nullptr || mObjectID.IsInvalid()) {
		return nullptr;
	}

	Object* live = gObjectManager->GetObject(mObjectID);
	return (live == mpObject) ? live : nullptr;
}

void ObjectCreationOperation::Undo(EditorSelection& selection)
{
	Object* created = ResolveTarget();
	if (created == nullptr) {
		return;
	}

	selection.Remove(created);

	gWorld->pBlockout->DestroyObject(created);

	Retarget(nullptr);
}

void ObjectCreationOperation::Redo(EditorSelection& selection)
{
	Execute(selection);

	selection.Add(ResolveTarget());
}

LightEditOperation::LightEditOperation(LightSpot* light, int32 group_size)
	: EditOperation(group_size), mpLight(light), mLightID((light != nullptr) ? light->ID : LightID::scNull)
{
}

LightSpot* LightEditOperation::ResolveLight() const
{
	if (mpLight == nullptr || mLightID.IsInvalid()) {
		return nullptr;
	}

	LightBase* live = gLightManager->GetLight(mLightID);
	return (live == mpLight) ? mpLight : nullptr;
}

void LightEditOperation::Restore(const LightSnapshot& snapshot)
{
	Ref<LightSpot> light = gLightManager->NewLight<LightSpot>(snapshot.LightName.Get());

	light->SetPosition(snapshot.Position);
	light->SetDirection(snapshot.Direction);
	light->SetRadius(snapshot.Radius);
	light->SetConeAngles(snapshot.InnerAngle, snapshot.OuterAngle);
	light->bCastShadows = snapshot.bCastShadows;
	light->Color = snapshot.Colour;
	light->Intensity = snapshot.Intensity;

	mpLight = &(*light);
	mLightID = light->ID;
}

void LightEditOperation::Destroy()
{
	if (ResolveLight() != nullptr) {
		gLightManager->RemoveLight(mLightID);
	}

	mpLight = nullptr;
}

/////////////////////////////////////
// Object operations
/////////////////////////////////////

MoveOperation::MoveOperation(Object* object, const Vec3f& before, const Vec3f& after, int32 group_size)
	: ObjectEditOperation(object, group_size), mBefore(before), mAfter(after)
{
}

void MoveOperation::Execute(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		target->SetPosition(mAfter);
	}
}

void MoveOperation::Undo(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		target->SetPosition(mBefore);
	}
}

RotateOperation::RotateOperation(Object* object, const Vec3f& euler_before, const Vec3f& euler_after, int32 group_size)
	: RotateOperation(object, Quat::FromEulerAngles(euler_before), Quat::FromEulerAngles(euler_after), group_size)
{
}

RotateOperation::RotateOperation(Object* object, const Quat& before, const Quat& after, int32 group_size)
	: ObjectEditOperation(object, group_size), mBefore(before), mAfter(after)
{
}

void RotateOperation::Execute(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		target->SetRotation(mAfter);
	}
}

void RotateOperation::Undo(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		target->SetRotation(mBefore);
	}
}

ScaleFaceOperation::ScaleFaceOperation(Object* object, const Vec3f& face_normal, float32 distance, int32 group_size)
	: ObjectEditOperation(object, group_size), mFaceNormal(face_normal), mDistance(distance)
{
}

void ScaleFaceOperation::Execute(EditorSelection& selection)
{
	Object* target = ResolveTarget();
	if (target == nullptr) {
		return;
	}

	const Brush* brush = gWorld->pBlockout->GetBrush(target);
	if (brush == nullptr) {
		return;
	}

	mPlanesBefore = brush->Planes;

	gWorld->pBlockout->MoveFace(target, mFaceNormal, mDistance);
}

void ScaleFaceOperation::Undo(EditorSelection& selection)
{
	Object* target = ResolveTarget();
	if (target == nullptr) {
		return;
	}

	// Restore the planes from before, as moving the face back could leave planes it dropped behind. The object is
	// put back where it was, as the brush is kept in place.
	gWorld->pBlockout->SetBrushPlanes(target, mPlanesBefore);
}

BrushEditOperation::BrushEditOperation(Object* object, const Brush::PlaneList& before, const Brush::PlaneList& after,
									   int32 group_size)
	: ObjectEditOperation(object, group_size), mPlanesBefore(before), mPlanesAfter(after)
{
}

void BrushEditOperation::Execute(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		gWorld->pBlockout->SetBrushPlanes(target, mPlanesAfter);
	}
}

void BrushEditOperation::Undo(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		gWorld->pBlockout->SetBrushPlanes(target, mPlanesBefore);
	}
}

BoundsEditOperation::BoundsEditOperation(Object* object, const BBox& before, const BBox& after, int32 group_size)
	: ObjectEditOperation(object, group_size), mBoundsBefore(before), mBoundsAfter(after)
{
}

void BoundsEditOperation::Execute(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		target->SetBounds(mBoundsAfter);
	}
}

void BoundsEditOperation::Undo(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		target->SetBounds(mBoundsBefore);
	}
}

ObjectStateEditOperation::ObjectStateEditOperation(Object* object, uint32 bit, bool is_tag, bool enabled,
												   int32 group_size)
	: ObjectEditOperation(object, group_size), mBit(bit), mbIsTag(is_tag), mbEnabled(enabled)
{
	mTagsBefore = static_cast<uint32>(object->Tags);
	mPositionBefore = object->GetPosition();
	mRotationBefore = object->mRotation;

	ForEachNode(
		*object, [this](Object& node)
		{ mNodesBefore.push_back(NodeFlags { .Id = node.ID, .Flags = static_cast<uint32>(node.GetFlags()) }); });
}

void ObjectStateEditOperation::Execute(EditorSelection& selection)
{
	Object* target = ResolveTarget();
	if (target == nullptr) {
		return;
	}

	if (mbIsTag) {
		ApplyTag(*target, mBit, mbEnabled);
	}
	else {
		ApplyFlag(*target, mBit, mbEnabled);
	}
}

void ObjectStateEditOperation::Undo(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		RestoreState(*target);
	}
}

void ObjectStateEditOperation::RestoreState(Object& root) const
{
	for (const eObjectTag tag : scEditableTags) {
		const uint32 tag_bit = static_cast<uint32>(tag);

		ApplyTag(root, tag_bit, (mTagsBefore & tag_bit) != 0);
	}

	for (const NodeFlags& saved : mNodesBefore) {
		Object* node = gObjectManager->GetObject(saved.Id);

		if (node == nullptr) {
			continue;
		}

		for (const eObjectFlags flag : scEditableFlags) {
			const uint32 flag_bit = static_cast<uint32>(flag);
			const bool wanted = (saved.Flags & flag_bit) != 0;

			if (CanEditObjectFlag(node, flag_bit) && HasFlag(node->GetFlags(), flag) != wanted) {
				if (flag == eObjectFlags::PhysicsEnabled && node == &root) {
					node->SetPosition(mPositionBefore);
					node->SetRotation(mRotationBefore);
				}

				SetObjectFlag(*node, flag, wanted, false);
			}
		}
	}
}

ScriptEditOperation::ScriptEditOperation(Object* object, const String& before, const String& after, int32 group_size)
	: ObjectEditOperation(object, group_size), mBefore(before), mAfter(after)
{
}

void ScriptEditOperation::Execute(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		gObjectScripts->Attach(target, mAfter);
	}
}

void ScriptEditOperation::Undo(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		gObjectScripts->Attach(target, mBefore);
	}
}

TriggerDirectionEditOperation::TriggerDirectionEditOperation(Object* object, const Vec3f& before, const Vec3f& after,
															 int32 group_size)
	: ObjectEditOperation(object, group_size), mBefore(before), mAfter(after)
{
}

void TriggerDirectionEditOperation::Execute(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		gObjectScripts->SetRequiredEnterDirection(target->ID, mAfter);
	}
}

void TriggerDirectionEditOperation::Undo(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		gObjectScripts->SetRequiredEnterDirection(target->ID, mBefore);
	}
}

/////////////////////////////////////
// Object creation and deletion
/////////////////////////////////////

CreateOperation::CreateOperation(const Vec3f& position, int32 group_size, MaterialID material)
	: ObjectCreationOperation(group_size), mPosition(position), mMaterial(material)
{
}

void CreateOperation::Execute(EditorSelection& selection)
{
	Retarget(gWorld->pBlockout->NewObject(mPosition, mMaterial));
}

CreateBrushOperation::CreateBrushOperation(const Brush::PlaneList& planes, const ObjectSnapshot& snapshot,
										   int32 group_size)
	: ObjectCreationOperation(group_size), mPlanes(planes), mSnapshot(snapshot)
{
}

void CreateBrushOperation::Execute(EditorSelection& selection) { Retarget(RestoreBrush(mSnapshot, mPlanes)); }

DupeOperation::DupeOperation(Object* original, const Vec3f& position, int32 group_size)
	: ObjectEditOperation(original, group_size), mPosition(position)
{
}

Object* DupeOperation::GetDupe() const
{
	if (mpDupe == nullptr || mDupeID.IsInvalid()) {
		return nullptr;
	}

	Object* live = gObjectManager->GetObject(mDupeID);
	return (live == mpDupe) ? live : nullptr;
}

void DupeOperation::Execute(EditorSelection& selection)
{
	mpDupe = nullptr;
	mDupeID = ObjectID::scNull;

	Object* original = ResolveTarget();
	if (original == nullptr) {
		return;
	}

	Object* dupe = gWorld->pBlockout->DupeObject(original);
	if (dupe == nullptr) {
		return;
	}

	dupe->SetPosition(mPosition);

	// DupeObject copies the source's *current* material, which is the selection
	// material while selected. Restore the original so dupes don't inherit it.
	dupe->SetMaterial(selection.GetStoredMaterial(original));

	mpDupe = dupe;
	mDupeID = dupe->ID;
}

void DupeOperation::Undo(EditorSelection& selection)
{
	Object* dupe = GetDupe();

	if (dupe != nullptr) {
		selection.Remove(dupe);

		gWorld->pBlockout->DestroyObject(dupe);

		mpDupe = nullptr;
		mDupeID = ObjectID::scNull;
	}

	selection.Add(ResolveTarget());
}

void DupeOperation::Redo(EditorSelection& selection)
{
	Execute(selection);

	selection.Remove(ResolveTarget());
	selection.Add(GetDupe());
}

DeleteOperation::DeleteOperation(Object* object, const Brush::PlaneList& planes, const ObjectSnapshot& snapshot,
								 int32 group_size)
	: ObjectEditOperation(object, group_size), mPlanes(planes), mSnapshot(snapshot)
{
}

void DeleteOperation::Execute(EditorSelection& selection)
{
	Object* target = ResolveTarget();
	if (target == nullptr) {
		return;
	}

	selection.Remove(target);

	gWorld->pBlockout->DestroyObject(target);
}

void DeleteOperation::Undo(EditorSelection& selection)
{
	Object* restored = RestoreBrush(mSnapshot, mPlanes);
	if (restored == nullptr) {
		return;
	}

	Retarget(restored);

	selection.Add(restored);
}

/////////////////////////////////////
// Light operations
/////////////////////////////////////

LightTransformOperation::LightTransformOperation(LightSpot* light, const Vec3f& position_before,
												 const Vec3f& position_after, const Vec3f& direction_before,
												 const Vec3f& direction_after)
	: LightEditOperation(light, 1), mPositionBefore(position_before), mPositionAfter(position_after),
	  mDirectionBefore(direction_before), mDirectionAfter(direction_after)
{
}

void LightTransformOperation::Execute(EditorSelection& selection)
{
	if (LightSpot* light = ResolveLight()) {
		light->SetPosition(mPositionAfter);
		light->SetDirection(mDirectionAfter);
	}
}

void LightTransformOperation::Undo(EditorSelection& selection)
{
	if (LightSpot* light = ResolveLight()) {
		light->SetPosition(mPositionBefore);
		light->SetDirection(mDirectionBefore);
	}
}

LightCreateOperation::LightCreateOperation(const LightSnapshot& snapshot)
	: LightEditOperation(nullptr, 1), mSnapshot(snapshot)
{
}

void LightCreateOperation::Execute(EditorSelection& selection) { Restore(mSnapshot); }

void LightCreateOperation::Undo(EditorSelection& selection) { Destroy(); }

LightDeleteOperation::LightDeleteOperation(LightSpot* light)
	: LightEditOperation(light, 1), mSnapshot(LightSnapshot::Capture(*light))
{
}

void LightDeleteOperation::Execute(EditorSelection& selection) { Destroy(); }

void LightDeleteOperation::Undo(EditorSelection& selection) { Restore(mSnapshot); }

/////////////////////////////////////
// Player spawn
/////////////////////////////////////

SpawnTransformOperation::SpawnTransformOperation(const State& before, const State& after)
	: EditOperation(1), mBefore(before), mAfter(after)
{
}

void SpawnTransformOperation::Execute(EditorSelection& selection)
{
	gWorld->PlayerSpawn.Position = mAfter.Position;
	gWorld->PlayerSpawn.Direction = mAfter.Direction;
	gWorld->PlayerSpawn.bCustom = mAfter.bCustom;
}

void SpawnTransformOperation::Undo(EditorSelection& selection)
{
	gWorld->PlayerSpawn.Position = mBefore.Position;
	gWorld->PlayerSpawn.Direction = mBefore.Direction;
	gWorld->PlayerSpawn.bCustom = mBefore.bCustom;
}


/////////////////////////////////////
// Edit History
/////////////////////////////////////

EditHistory::EditHistory(EditorSelection& selection) : mSelection(selection)
{
	mOperations.InitCapacity(scMaxOperations);
}

EditOperation& EditHistory::Push(std::unique_ptr<EditOperation> op)
{
	// Make room by dropping the oldest groups whole, so that no group is left half undoable
	const uint32 capacity = mOperations.GetCapacity();

	while (capacity > 0 && mOperations.GetSize() >= capacity) {
		const uint32 size = mOperations.GetSize();
		const uint32 evict = std::min(static_cast<uint32>(std::max(mOperations.First()->GroupSize, 1)), size);

		for (uint32 i = 0; i < evict; i++) {
			mOperations.PopFront();
		}
	}

	EditOperation& pushed = *mOperations.Push(std::move(op));

	pushed.Execute(mSelection);

	return pushed;
}

bool EditHistory::Undo()
{
	bool selection_changed = false;

	// Every operation in a group carries the size of the group. It is clamped so that one bad operation can't unwind
	// the whole stack.
	int32 remaining = 1;

	for (bool first = true; remaining > 0; remaining--, first = false) {
		std::unique_ptr<EditOperation>* slot = mOperations.Undo();
		if (slot == nullptr) {
			break;
		}

		EditOperation& op = **slot;

		if (first) {
			remaining = std::max(op.GroupSize, 1);
		}

		op.Undo(mSelection);

		selection_changed |= op.ChangesObjects();
	}

	return selection_changed;
}

bool EditHistory::Redo()
{
	bool selection_changed = false;

	int32 remaining = 1;

	for (bool first = true; remaining > 0; remaining--, first = false) {
		std::unique_ptr<EditOperation>* slot = mOperations.Redo();
		if (slot == nullptr) {
			break;
		}

		EditOperation& op = **slot;

		if (first) {
			remaining = std::max(op.GroupSize, 1);
		}

		op.Redo(mSelection);

		selection_changed |= op.ChangesObjects();
	}

	return selection_changed;
}

void EditHistory::Clear() { mOperations.Clear(); }

} // namespace fx::editor
