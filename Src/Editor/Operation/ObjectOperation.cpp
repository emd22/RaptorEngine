#include "ObjectOperation.hpp"

#include <Blockout.hpp>
#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <Script/ObjectScripts.hpp>
#include <World.hpp>


namespace fx {

namespace editor {


static constexpr eObjectTag scEditableTags[] = {
	eObjectTag::LockTransform, eObjectTag::ProbeVolume, eObjectTag::ReflectionProbe, eObjectTag::Spawn,
	eObjectTag::Trigger,	   eObjectTag::NvBrush,		eObjectTag::DBound,
};

static constexpr eObjectFlags scEditableFlags[] = {
	eObjectFlags::PhysicsEnabled,
	eObjectFlags::ShadowCaster,
	eObjectFlags::DisableCulling,
	eObjectFlags::NotProbeVisible,
};

bool CanEditObjectTag(const Object* object, eObjectTag tag)
{
	if (object == nullptr) {
		return false;
	}

	switch (tag) {
	case eObjectTag::LockTransform:
	case eObjectTag::Spawn:
		return true;
	case eObjectTag::ProbeVolume:
	case eObjectTag::ReflectionProbe:
	case eObjectTag::Trigger:
	case eObjectTag::NvBrush:
	case eObjectTag::DBound:
		return object->HasTags(eObjectTag::Blockout) && !object->IsDecal() && !object->IsPatch();
	default:
		return false;
	}
}

eObjectTag GetEditableTags(const Object* object)
{
	eObjectTag editable = eObjectTag::None;

	for (const eObjectTag tag : scEditableTags) {
		if (CanEditObjectTag(object, tag)) {
			editable |= tag;
		}
	}

	return editable;
}

eObjectFlags GetEditableFlags(const Object* object)
{
	eObjectFlags editable = eObjectFlags::None;

	for (const eObjectFlags flag : scEditableFlags) {
		if (CanEditObjectFlag(object, flag)) {
			editable |= flag;
		}
	}

	return editable;
}

bool CanAttachScript(const Object* object)
{
	if (object == nullptr || gWorld->pPrototype == nullptr) {
		return false;
	}

	return object->HasTags(eObjectTag::Blockout) || gWorld->pPrototype->IsModel(object);
}

bool CanEditObjectFlag(const Object* object, eObjectFlags flag)
{
	if (object == nullptr) {
		return false;
	}

	switch (flag) {
	case eObjectFlags::DisableCulling:
		return true;
	case eObjectFlags::ShadowCaster:
	case eObjectFlags::NotProbeVisible:
		return !object->IsVolume() && !object->IsDecal();
	case eObjectFlags::PhysicsEnabled: {
		if (object->IsDecal()) {
			return false;
		}

		if (object->HasTags(eObjectTag::Blockout)) {
			return !object->IsVolume() && gWorld->pPrototype != nullptr && gWorld->pPrototype->HasBrush(object);
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
	case eObjectTag::NvBrush:
		object.SetNvBrush(enabled);
		break;
	case eObjectTag::DBound:
		object.SetDBound(enabled);

		if (gWorld->pPrototype != nullptr) {
			gEditor->SetStoredMaterial(&object, enabled ? gWorld->pPrototype->GetDBoundMaterial()
														: gWorld->pPrototype->GetDefaultMaterial());
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
		if (!object.HasTags(eObjectTag::Blockout) || gWorld->pPrototype == nullptr ||
			!gWorld->pPrototype->SetDynamic(&object, enabled)) {
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

static void ApplyTag(Object& object, eObjectTag tag, bool enabled)
{
	if (!CanEditObjectTag(&object, tag) || object.HasTags(tag) == enabled) {
		return;
	}

	const bool was_reflection_probe = object.IsReflectionProbe();

	SetObjectTag(object, tag, enabled);

	if (was_reflection_probe != object.IsReflectionProbe()) {
		gProbeManager->RebuildReflectionProbesFromWorld();
	}
}

static void ApplyFlag(Object& object, eObjectFlags flag, bool enabled)
{
	if (!CanEditObjectFlag(&object, flag) || HasFlag(object.GetFlags(), flag) == enabled) {
		return;
	}

	SetObjectFlag(object, flag, enabled, true);
}

static Object* RestoreBrush(const ObjectSnapshot& snapshot, const Brush::PlaneList& planes)
{
	if (gWorld->pPrototype == nullptr) {
		return nullptr;
	}

	Object* object = nullptr;

	if (snapshot.pPatch != nullptr) {
		object = gWorld->pPrototype->RestorePatch(snapshot.Position, *snapshot.pPatch, snapshot.Material,
												  snapshot.Rotation, snapshot.ObjectName);
	}
	else {
		object = gWorld->pPrototype->RestoreObject(snapshot.Position, planes, snapshot.Material, snapshot.Rotation,
												   snapshot.ObjectName, snapshot.bIsDynamic, snapshot.bIsDecal);
	}

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

	if (snapshot.bIsNvBrush) {
		object->SetNvBrush(true);
	}

	if (snapshot.bIsDBound) {
		object->SetDBound(true);
	}

	gObjectScripts->Attach(object, snapshot.ScriptPath);
	gObjectScripts->SetRequiredEnterDirection(object->ID, snapshot.EnterDirection);

	if (snapshot.SubviewInstance != 0) {
		object->SubviewInstance = snapshot.SubviewInstance;
		object->SubviewMember = snapshot.SubviewMember;
	}

	return object;
}


ObjectSnapshot ObjectSnapshot::Capture(const Object& object, MaterialID material)
{
	Vec3f enter_direction = Vec3f::sZero;
	gObjectScripts->TryGetRequiredEnterDirection(object.ID, enter_direction);

	std::shared_ptr<const Patch> patch;
	const Patch* live_patch = gWorld->pPrototype->GetPatch(&object);

	if (live_patch != nullptr) {
		patch = std::make_shared<const Patch>(live_patch->Clone());
	}

	return ObjectSnapshot {
		.Position = object.GetPosition(),
		.Material = material,
		.Rotation = object.mRotation,
		.ObjectName = object.Name,
		.bIsProbeVolume = object.IsProbeVolume(),
		.bIsReflectionProbe = object.IsReflectionProbe(),
		.bIsSpawn = object.IsSpawn(),
		.bIsTrigger = object.IsTrigger(),
		.bIsNvBrush = object.IsNvBrush(),
		.bIsDBound = object.IsDBound(),
		.bIsDecal = object.IsDecal(),
		.bIsDynamic = gWorld->pPrototype->IsDynamic(&object),
		.ScriptPath = gObjectScripts->GetPath(object.ID),
		.EnterDirection = enter_direction,
		.SubviewInstance = object.SubviewInstance,
		.SubviewMember = object.SubviewMember,
		.pPatch = std::move(patch),
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

	gWorld->pPrototype->DestroyObject(created);

	Retarget(nullptr);
}

void ObjectCreationOperation::Redo(EditorSelection& selection)
{
	Execute(selection);

	selection.Add(ResolveTarget());
}


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

	const Brush* brush = gWorld->pPrototype->GetBrush(target);
	if (brush == nullptr) {
		return;
	}

	mPlanesBefore = brush->Planes;

	gWorld->pPrototype->MoveFace(target, mFaceNormal, mDistance);
}

void ScaleFaceOperation::Undo(EditorSelection& selection)
{
	Object* target = ResolveTarget();
	if (target == nullptr) {
		return;
	}

	// Restore the planes from before, as moving the face back could leave planes it dropped behind. The object is
	// put back where it was, as the brush is kept in place.
	gWorld->pPrototype->SetBrushPlanes(target, mPlanesBefore);
}

BrushEditOperation::BrushEditOperation(Object* object, const Brush::PlaneList& before, const Brush::PlaneList& after,
									   int32 group_size)
	: ObjectEditOperation(object, group_size), mPlanesBefore(before), mPlanesAfter(after)
{
}

void BrushEditOperation::Execute(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		gWorld->pPrototype->SetBrushPlanes(target, mPlanesAfter);
	}
}

PatchEditOperation::PatchEditOperation(Object* object, Patch&& before, Patch&& after, int32 group_size)
	: ObjectEditOperation(object, group_size), mpBefore(std::make_shared<const Patch>(std::move(before))),
	  mpAfter(std::make_shared<const Patch>(std::move(after)))
{
}

void PatchEditOperation::Execute(EditorSelection& selection)
{
	Object* target = ResolveTarget();

	if (target != nullptr) {
		gWorld->pPrototype->SetPatch(target, mpAfter->Clone());
	}
}

void PatchEditOperation::Undo(EditorSelection& selection)
{
	Object* target = ResolveTarget();

	if (target != nullptr) {
		gWorld->pPrototype->SetPatch(target, mpBefore->Clone());
	}
}

void BrushEditOperation::Undo(EditorSelection& selection)
{
	if (Object* target = ResolveTarget()) {
		gWorld->pPrototype->SetBrushPlanes(target, mPlanesBefore);
	}
}

ObjectStateEditOperation::ObjectStateEditOperation(Object* object, eObjectTag tag, bool enabled, int32 group_size)
	: ObjectEditOperation(object, group_size), mTag(tag), mbEnabled(enabled)
{
	CaptureState(*object);
}

ObjectStateEditOperation::ObjectStateEditOperation(Object* object, eObjectFlags flag, bool enabled, int32 group_size)
	: ObjectEditOperation(object, group_size), mFlag(flag), mbEnabled(enabled)
{
	CaptureState(*object);
}

void ObjectStateEditOperation::CaptureState(Object& object)
{
	mTagsBefore = object.Tags;
	mPositionBefore = object.GetPosition();
	mRotationBefore = object.mRotation;

	ForEachNode(object,
				[this](Object& node) { mNodesBefore.Insert(NodeFlags { .Id = node.ID, .Flags = node.GetFlags() }); });
}

void ObjectStateEditOperation::Execute(EditorSelection& selection)
{
	Object* target = ResolveTarget();
	if (target == nullptr) {
		return;
	}

	if (mTag != eObjectTag::None) {
		ApplyTag(*target, mTag, mbEnabled);
	}
	else {
		ApplyFlag(*target, mFlag, mbEnabled);
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
		ApplyTag(root, tag, HasFlag(mTagsBefore, tag));
	}

	for (const NodeFlags& saved : mNodesBefore) {
		Object* node = gObjectManager->GetObject(saved.Id);

		if (node == nullptr) {
			continue;
		}

		for (const eObjectFlags flag : scEditableFlags) {
			const bool wanted = HasFlag(saved.Flags, flag);

			if (CanEditObjectFlag(node, flag) && HasFlag(node->GetFlags(), flag) != wanted) {
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
	Retarget(gWorld->pPrototype->NewObject(mPosition, mMaterial));
}

CreateBrushOperation::CreateBrushOperation(const Brush::PlaneList& planes, const ObjectSnapshot& snapshot,
										   int32 group_size)
	: ObjectCreationOperation(group_size), mPlanes(planes), mSnapshot(snapshot)
{
}

void CreateBrushOperation::Execute(EditorSelection& selection) { Retarget(RestoreBrush(mSnapshot, mPlanes)); }

DupeOperation::DupeOperation(Object* original, const Vec3f& position, int32 group_size, uint32 dupe_subview)
	: ObjectEditOperation(original, group_size), mPosition(position), mDupeSubview(dupe_subview)
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

	if (mDupeSubview != 0) {
		Object* placed = gWorld->pPrototype->PlaceSubviewInstance(mDupeSubview,
																  BrushPlacement { mPosition, original->mRotation });

		if (placed != nullptr) {
			mpDupe = placed;
			mDupeID = placed->ID;
		}

		return;
	}

	Object* dupe = gWorld->pPrototype->DupeObject(original);
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

		if (mDupeSubview != 0) {
			gWorld->pPrototype->RemoveSubviewInstance(mDupeSubview);
		}
		else {
			gWorld->pPrototype->DestroyObject(dupe);
		}

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

	gWorld->pPrototype->DestroyObject(target);
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


} // namespace editor


} // namespace fx
