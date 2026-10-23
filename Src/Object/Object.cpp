#include "Object.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <Core/RefUtil.hpp>
#include <Decal/DecalManager.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Math/RayCast.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/JoltPhysicsBackend.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/LightProbe.hpp>
#include <Renderer/MeshUtil.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <Script/ObjectScripts.hpp>
#include <World.hpp>
#include <algorithm>
#include <utility>
#include <vector>

namespace fx {

using namespace renderer;

Object::Object(const ObjectID id) { ID = id; }

Object::Object(const ObjectID id, const MaterialID material)
{
	ID = id;
	mMaterialID = material;
}

Object::Object(const ObjectID id, const MaterialID material, bool is_loading)
{
	ID = id;
	mMaterialID = material;
	bIsLoading.store(is_loading);
}

void Object::SetMaterial(const MaterialID& id)
{
	if (mMaterialID.GetID() == id.GetID()) {
		return;
	}

	const eSpecialMaterial previous_special = GetSpecialMaterial();

	mMaterialID = id;

	if (IsBrushMarkerMaterial(previous_special) || IsBrushMarkerMaterial(GetSpecialMaterial())) {
		ApplyVolumeState();
	}

	// if (bIsAddedToWorld && !ID.IsInvalid() && pMesh.IsValid()) {
	// 	gWorld->NotifyObjectMaterialChanged(ID);
	// }
}

void Object::Create(const Ref<PrimitiveMesh>& mesh, const MaterialID& material)
{
	pMesh = mesh;

	// Directly set the material to avoid the ol' `SetMaterial` curse
	mMaterialID = material;
}

bool Object::CheckIfReady(bool require_material)
{
	if (HasFlag(Flags, eObjectFlags::ReadyToRender)) {
		return true;
	}

	// This is not a container object, just check that the mesh is loaded
	if (!pMesh || !pMesh->bIsReady) {
		ClearFlag(Flags, eObjectFlags::ReadyToRender);
		return false;
	}

	Material* material = gMaterialManager->GetMaterial(mMaterialID);
	if (material == nullptr) {
		return false;
	}

	// Check material is ready
	if (!material->bReadyToCheck.test()) {
		return false;
	}

	SetFlag(Flags, eObjectFlags::ReadyToRender);
	LogInfo(LC_RENDER, "Object {} is now ready to render.", Name.Get());

	FinalizeWhenReady();

	if (gWorldGrid != nullptr) {
		gWorldGrid->MarkObjectDirty(this);
	}

	return true;
}


void Object::FinalizeWhenReady()
{
	if (!ParentID.IsNull()) {
		Object* parent_object = gObjectManager->GetObject(ParentID);

		if (parent_object != nullptr) {
			parent_object->Bounds.Add(Bounds);
			parent_object->InvalidateDrawCopies();
		}
	}

	if (mMaterialID.IsNull()) {
		return;
	}

	Material* material = gMaterialManager->GetMaterial(mMaterialID);
	if (material == nullptr) {
		return;
	}

	if (HasFlag(Flags, eObjectFlags::Unlit)) {
		material->SetUnlit(true);
	}
}


void Object::OnAttached(World* scene)
{
	physics::Body* phys = gPhysics->GetBody(PhysicsID);

	// When the object is attached to the scene, enable physics if the physics object is active.
	if (phys && phys->mbHasPhysicsBody) {
		SetPhysicsEnabled(gPhysics->pBackend->GetBodyInterface().IsActive(phys->GetBodyID()));
	}
}


void Object::AttachToBone(const Ref<Skeleton>& skeleton, BoneId bone, const Mat4f& offset)
{
	pAttachSkeleton = skeleton;
	AttachBone = bone;
	AttachOffset = offset;
	mbAttachPosed = false;
}

void Object::UpdateBoneAttachment()
{
	Skeleton& skel = *pAttachSkeleton;

	if (AttachBone >= skel.JointCount) {
		return;
	}

	skel.AdvancePose(gGraphics->DeltaTime);

	if (mbAttachPosed && skel.PoseHash == mLastAttachPoseHash) {
		return;
	}

	mbAttachPosed = true;
	mLastAttachPoseHash = skel.PoseHash;

	SetPreTransform(AttachOffset * skel.WorldTransforms[AttachBone]);
	InvalidateDrawCopies();
	gWorldGrid->MarkObjectDirty(this);
}

void Object::UpdateAnimation()
{
	if (pAttachSkeleton) {
		UpdateBoneAttachment();
	}

	if (!pSkeleton) {
		BoneBufferBase = Skeleton::scNoBones;
		return;
	}

	// A skeleton can be visited multiple times in one frame (several meshes sharing it, and the shadow pass, depth
	// prepass and forward pass for each); it only advances the first time, and every subsequent draw this frame reuses
	// the bone-buffer slots claimed then.
	Skeleton& skel = *pSkeleton;
	skel.Update(gGraphics->DeltaTime);

	BoneBufferBase = skel.BoneBufferBase;

	if (skel.PoseHash != mLastPoseHash) {
		mLastPoseHash = skel.PoseHash;
		gWorldGrid->MarkObjectDirty(this);
	}
}

struct SkeletonCloneMap
{
	std::vector<std::pair<const Skeleton*, Ref<Skeleton>>> Entries;
};

Object* Object::CloneNode(const String& name, SkeletonCloneMap& skeletons) const
{
	Object* clone = gObjectManager->NewObject(name, mMaterialID, Tags);

	clone->pMesh = pMesh;
	clone->Bounds = Bounds;
	clone->mPosition = mPosition;
	clone->mRotation = mRotation;
	clone->mScale = mScale;
	clone->mObjectLayer = mObjectLayer;

	clone->Flags = Flags;
	ClearFlag(clone->Flags, (eObjectFlags::ReadyToRender | eObjectFlags::PhysicsEnabled));

	clone->mInstances.Assign(mInstances.pData, mInstances.Size);

	clone->MarkTransformOutOfDate();

	const auto clone_skeleton = [&skeletons](const Ref<Skeleton>& source) -> Ref<Skeleton>
	{
		const Skeleton* key = &(*source);

		auto found = std::find_if(skeletons.Entries.begin(), skeletons.Entries.end(),
								  [key](const auto& entry) { return entry.first == key; });

		if (found == skeletons.Entries.end()) {
			skeletons.Entries.emplace_back(key, Skeleton::CreateInstance(source));
			found = skeletons.Entries.end() - 1;
		}

		return found->second;
	};

	if (pSkeleton.IsValid()) {
		clone->pSkeleton = clone_skeleton(pSkeleton);
	}

	if (pAttachSkeleton.IsValid()) {
		clone->AttachToBone(clone_skeleton(pAttachSkeleton), AttachBone, AttachOffset);
	}

	if (!AttachedNodes.IsEmpty()) {
		clone->AttachedNodes.Create(8);

		for (const ObjectID& child_id : AttachedNodes) {
			const Object* child = gObjectManager->GetObject(child_id);

			if (child == nullptr) {
				continue;
			}

			Object* child_clone = child->CloneNode((child->Name.Get()), skeletons);
			child_clone->ParentID = clone->ID;

			clone->AttachedNodes.Insert(child_clone->ID);
		}
	}

	return clone;
}

Object* Object::CloneWithOwnSkeleton(const std::string& name) const
{
	SkeletonCloneMap skeletons;
	return CloneNode(name, skeletons);
}

static Mat4f MakeInstanceTransform(const Vec3f position, const Quat rotation, float32 scale)
{
	return Mat4f::AsScale(Vec3f(scale)) * Mat4f::AsRotation(rotation) * Mat4f::AsTranslation(position);
}

uint32 Object::AddInstance(const Vec3f position, const Quat rotation, float32 scale)
{
	return AddInstance(MakeInstanceTransform(position, rotation, scale));
}

uint32 Object::AddInstance(const Mat4f& transform)
{
	mInstances.Insert(transform);
	QueueInstanceRefresh();

	return mInstances.Size - 1;
}

void Object::SetInstance(uint32 index, const Vec3f position, const Quat rotation, float32 scale)
{
	SetInstance(index, MakeInstanceTransform(position, rotation, scale));
}

void Object::SetInstance(uint32 index, const Mat4f& transform)
{
	AssertMsg(index < mInstances.Size, "Instance index out of range");

	mInstances[index] = transform;
	QueueInstanceRefresh();
}

void Object::RemoveInstance(uint32 index)
{
	AssertMsg(index < mInstances.Size, "Instance index out of range");

	mInstances[index] = mInstances[mInstances.Size - 1];
	mInstances.RemoveLast();
	QueueInstanceRefresh();
}

void Object::ClearInstances()
{
	if (mInstances.IsEmpty()) {
		return;
	}

	mInstances.Clear();
	QueueInstanceRefresh();
}

void Object::QueueInstanceRefresh()
{
	if (mbInstancesPending) {
		return;
	}

	mbInstancesPending = true;
	gObjectManager->QueueInstanceUpdate(ID);
}

void Object::ApplyInstanceChanges()
{
	mbInstancesPending = false;

	InvalidateDrawCopies();
	gWorldGrid->UpdateObject(this, false);
	UpdateInstanceCollider();

	for (ObjectID attached_id : AttachedNodes) {
		Object* attached_object = gObjectManager->GetObject(attached_id);

		if (attached_object != nullptr) {
			attached_object->ApplyInstanceChanges();
		}
	}
}

void Object::UpdateInstanceCollider()
{
	physics::Body* body = gPhysics->GetBody(PhysicsID);

	if (body == nullptr || !body->mbHasPhysicsBody) {
		return;
	}

	Object* source = FindInstanceSource();

	if (source == nullptr) {
		body->SetInstanceTransforms(nullptr, 0);
		return;
	}

	const Mat4f body_world = Mat4f::AsRotation(body->GetRotation()) * Mat4f::AsTranslation(body->GetPosition());
	const Mat4f& source_world = source->GetWorldMatrix();

	const Mat4f to_source = body_world * source_world.Inverse();
	const Mat4f from_source = source_world * body_world.Inverse();

	SizedArray<Mat4f> transforms;
	transforms.InitCapacity(source->mInstances.Size);

	for (const Mat4f& instance : source->mInstances) {
		transforms.Insert(to_source * instance * from_source);
	}

	body->SetInstanceTransforms(transforms.pData, transforms.Size);
}

Object* Object::FindInstanceSource()
{
	Object* node = this;

	while (node != nullptr) {
		if (node->HasInstances()) {
			return node;
		}

		if (node->ParentID.IsNull() || node->ParentID.IsInvalid()) {
			return nullptr;
		}

		node = gObjectManager->GetObject(node->ParentID);
	}

	return nullptr;
}

void Object::InvalidateDrawCopies()
{
	mbDrawBoundsDirty = true;
	mDrawSlotsFrame = UINT32_MAX;
}

Mat4f Object::GetNodeToSource(Object& source)
{
	if (&source == this) {
		return Mat4f::scIdentity;
	}

	return GetWorldMatrix() * source.GetWorldMatrix().Inverse();
}

const BBox& Object::GetInstancedDrawBounds(Object& source)
{
	if (!mbDrawBoundsDirty) {
		return mDrawBounds;
	}

	const Mat4f node_to_source = GetNodeToSource(source);
	const Mat4f& source_world = source.GetWorldMatrix();

	for (uint32 index = 0; index < source.mInstances.Size; index++) {
		const Mat4f world = node_to_source * source.mInstances[index] * source_world;
		const BBox copy_bounds = OBBox::FromLocalBounds(Bounds, world).GetWorldAABB();

		if (index == 0) {
			mDrawBounds = copy_bounds;
		}
		else {
			mDrawBounds.Add(copy_bounds);
		}
	}

	mbDrawBoundsDirty = false;

	return mDrawBounds;
}

void Object::SetFixedArea(int32 area)
{
	mAreas = AreaSet::MakeEmpty();

	if (area < 0) {
		mAreas = AreaSet {};
	}
	else {
		mAreas.Add(static_cast<uint32>(area));
	}

	mAreas.bFixed = true;
}

BBox Object::GetDrawBounds()
{
	Object* source = FindInstanceSource();

	if (source == nullptr) {
		return GetWorldOBB().GetWorldAABB();
	}

	return GetInstancedDrawBounds(*source);
}

void Object::UpdateDrawSlots()
{
	const uint32 frame = gGraphics->GetElapsedFrameCount();

	if (mDrawSlotsFrame == frame) {
		return;
	}

	mDrawSlotsFrame = frame;
	mDrawFirstSlot = ID.GetID();
	mDrawCount = 1;

	Object* source = FindInstanceSource();

	if (source == nullptr) {
		return;
	}

	const uint32 count = source->mInstances.Size;
	ObjectGpuEntry* slots = gObjectManager->ReserveInstanceSlots(count, &mDrawFirstSlot);

	if (slots == nullptr) {
		static bool sbWarned = false;

		if (!sbWarned) {
			LogWarning(LC_RENDER, "Out of instance slots ({}), '{}' will not be drawn", ObjectManager::scMaxInstances,
					   Name.Get());
			sbWarned = true;
		}

		mDrawCount = 0;
		return;
	}

	const Mat4f node_to_source = GetNodeToSource(*source);
	const Mat4f& source_world = source->GetWorldMatrix();

	for (uint32 index = 0; index < count; index++) {
		const Mat4f world = node_to_source * source->mInstances[index] * source_world;
		memcpy(slots[index].ModelMatrix, world.RawData, sizeof(Mat4f));
	}

	mDrawCount = count;
}

uint32 Object::GetDrawIndex()
{
	UpdateDrawSlots();
	return mDrawFirstSlot;
}

uint32 Object::GetDrawCount()
{
	UpdateDrawSlots();
	return mDrawCount;
}


void Object::RenderShallow(const Camera& camera, renderer::Pipeline* pipeline)
{
	UpdateIfOutOfDate();

	if (IsHidden() || !CheckIfReady(true) || !HasBonesForDraw()) {
		return;
	}

	Assert(pipeline != nullptr);

	FrameData* frame = gGraphics->GetFrame();

	DrawPushConstants push_constants { .TargetSize = { gGraphics->Swapchain.Extent.X, gGraphics->Swapchain.Extent.Y } };
	push_constants.ObjectId = GetDrawIndex();

	push_constants.MaterialIndex = mMaterialID.GetID();
	push_constants.TileColumns = gGraphics->pRenderer->GetLightTileColumns();
	push_constants.TileRows = gGraphics->pRenderer->GetLightTileRows();
	push_constants.BoneBase = BoneBufferBase;


	// Only the probe capture faces themselves get the capture flag. Keying it off
	// IsCapturePending() would also strip probe GI and SSAO from the main view for
	// every frame of a grid bake.
	if (gProbeManager != nullptr && gProbeManager->IsCapturingFaces()) {
		push_constants.Flags |= eDrawFlags::ProbeCapture;

		if (gProbeManager->IsCapturingBounce()) {
			push_constants.Flags |= eDrawFlags::ProbeBounce;
		}

		if (gProbeManager->IsCapturingReflection()) {
			push_constants.Flags |= eDrawFlags::ReflectionCapture;
		}
	}

	const bool is_probe_capture = HasFlag(push_constants.Flags, eDrawFlags::ProbeCapture);
	push_constants.PreExposure = is_probe_capture ? 1.0f : gGraphics->PreExposure;

	if (gGraphics->bBakedSunActive && !is_probe_capture) {
		push_constants.Flags |= eDrawFlags::BakedSun;
	}

	if (gGraphics->bBakedIndirectActive && !is_probe_capture) {
		push_constants.Flags |= eDrawFlags::BakedIndirect;
	}

	if (pSkeleton.IsValid() && !is_probe_capture) {
		gDecalManager->GetSkinnedDecalRange(&(*pSkeleton), push_constants.SkinnedDecalStart,
											push_constants.SkinnedDecalCount);
	}

	if (gGraphics->bOnlyRenderProbes) {
		push_constants.Flags |= eDrawFlags::DebugIrradiance;
	}

	if (gGraphics->bRenderProbeVisibility) {
		push_constants.Flags |= eDrawFlags::DebugProbeVisibility;
	}

	if (gGraphics->bDisableReflectionProbes) {
		push_constants.Flags |= eDrawFlags::NoReflectionProbes;
	}

	if (!is_probe_capture && gGraphics->ReflectionDebugView == 1) {
		push_constants.Flags |= eDrawFlags::DebugReflection;
	}

	if (!is_probe_capture && gGraphics->ReflectionDebugView == 2) {
		push_constants.Flags |= eDrawFlags::DebugReflectionCoverage;
	}

	// Decals are placed in the world, and the view model only lines up with the world from the camera's position
	if (mObjectLayer != eObjectLayer::WorldLayer) {
		push_constants.Flags |= eDrawFlags::NoDecals;
	}

	memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(mObjectLayer).RawData, sizeof(Mat4f));
	memcpy(push_constants.EyePosition, camera.Position.mData, sizeof(float32) * 3);

	gGraphics->SubmitPushConstants(frame->CmdBuffer, *pipeline, eShaderType::Vertex | eShaderType::Pixel,
								   push_constants);

	RenderMesh(pipeline);
}


void Object::RenderPrimitive(const CommandBuffer& cmd)
{
	if (!pMesh || IsHidden() || !CheckIfReady(false) || !HasBonesForDraw()) {
		return;
	}

	const uint32 draw_count = GetDrawCount();

	if (draw_count > 0) {
		pMesh->Render(cmd, draw_count);
	}
}

void Object::RenderMesh(renderer::Pipeline* pipeline)
{
	FrameData* frame = gGraphics->GetFrame();
	CommandBuffer& cmd = frame->CmdBuffer;

	// If there was an error binding the object material, bind the null material.
	if (!gMaterialManager->BindWithPipeline(cmd, *pipeline, mMaterialID)) {
		gMaterialManager->BindWithPipeline(cmd, *pipeline, MaterialID::scNull);
	}

	if (!pMesh) {
		return;
	}

	const uint32 draw_count = GetDrawCount();

	if (draw_count > 0) {
		pMesh->Render(cmd, draw_count);
	}
}

bool Object::HasTransparentMaterial() const
{
	const Material* material = gMaterialManager->GetMaterial(mMaterialID);

	return (material != nullptr && material->IsTransparent());
}

bool Object::CanBeFrustumCulled() const
{
	if (!IsCullable() || !pMesh.IsValid() || mObjectLayer != eObjectLayer::WorldLayer) {
		return false;
	}

	if (IsSkinned()) {
		return false;
	}

	if (HasFlag(Flags, eObjectFlags::PhysicsEnabled)) {
		return false;
	}

	return (Bounds.Max.X > Bounds.Min.X) || (Bounds.Max.Y > Bounds.Min.Y) || (Bounds.Max.Z > Bounds.Min.Z);
}

bool Object::CanBePortalCulled() { return CanBeFrustumCulled() && FindInstanceSource() == nullptr; }

bool Object::IsOutsideFrustum(const Frustum& frustum, uint32 plane_mask)
{
	if (!CanBeFrustumCulled()) {
		return false;
	}

	Object* source = FindInstanceSource();

	if (source != nullptr) {
		return !frustum.IntersectsAABB(GetInstancedDrawBounds(*source), plane_mask);
	}

	return !frustum.IntersectsOBB(GetWorldOBB(), plane_mask);
}

void Object::Update()
{
	if (HasFlag(Flags, eObjectFlags::PhysicsEnabled)) {
		physics::Body* phys = gPhysics->GetBody(PhysicsID);

		if (mbPhysicsTransformOutOfDate) {
			phys->Teleport(mPosition, mRotation);
			mbPhysicsTransformOutOfDate = false;
		}

		SyncObjectWithPhysics(phys);
	}

	// The transformation has changed, we should tell the worldgrid
	if (mbMatrixOutOfDate) {
		InvalidateDrawCopies();
		gWorldGrid->UpdateObject(this);
	}


	// if (IsFlagSet(PendingFlags, eObjectFlags::Unlit) && !mMaterialID.IsNull()) {
	//     if (gMaterialManager->GetMaterial(mMaterialID)->IsReady()) {
	//         ClearFlag(PendingFlags, eObjectFlags::Unlit);

	//         if (!IsFlagSet(Flags, eObjectFlags::Unlit)) {
	//             SetGraphicsPipeline(nullptr);
	//         }
	//         else {
	//             SetGraphicsPipeline(&gPipelineCache->Request(ePipelineName::Unlit));
	//         }
	//     }
	// }

	UpdateAnimation();
}

float32 Object::GetDirectionScale(const Vec3f direction)
{
	if (direction.IsCloseTo(simd::LoadFloat4(0.0f))) {
		return 0.0f;
	}

	Vec3f dir = direction.Normalize();

	Vec3f pos_extent = Bounds.Max;
	Vec3f neg_extent = -Bounds.Min;

	Vec3f extent((dir.X >= 0.0f) ? pos_extent.X : neg_extent.X, (dir.Y >= 0.0f) ? pos_extent.Y : neg_extent.Y,
				 (dir.Z >= 0.0f) ? pos_extent.Z : neg_extent.Z);

	float32 distance = dir.Abs().Dot(extent);
	return distance * mScale;
}


void Object::AttachObject(const ObjectID& attach_id)
{
	if (!AttachedNodes.IsInited()) {
		AttachedNodes.Create(8);
	}

	Object* attached_object = gObjectManager->GetObject(attach_id);
	attached_object->ParentID = ID;
	attached_object->MoveBy(mPosition);
	attached_object->ScaleBy(mScale);

	AttachedNodes.Insert(attach_id);
}

void Object::SyncObjectWithPhysics(physics::Body* phys)
{
	const Vec3f physics_position = phys->GetPosition() - phys->Midpoint;

	if ((!mPosition.IsCloseTo(physics_position) || !mRotation.IsCloseTo(phys->GetRotation()))) {
		mPosition = physics_position;
		mRotation = phys->GetRotation();

		MarkMatrixOutOfDate();
	}
}

void Object::SetUnlit(const bool value)
{
	if (value) {
		SetFlag(Flags, eObjectFlags::Unlit);
	}
	else {
		ClearFlag(Flags, eObjectFlags::Unlit);
	}
}

void Object::AttachCollider(physics::Body* body)
{
	if (body == nullptr) {
		return;
	}

	PhysicsID = body->GetID();
	body->SetObjectID(ID);

	Object* source = FindInstanceSource();

	if (source != nullptr) {
		source->QueueInstanceRefresh();
	}
}

void Object::SetObjectLayer(eObjectLayer layer)
{
	mObjectLayer = layer;

	for (ObjectID attached_id : AttachedNodes) {
		Object* attached_object = gObjectManager->GetObject(attached_id);
		if (attached_object != nullptr) {
			attached_object->SetObjectLayer(layer);
		}
	}
}

void Object::SetProbeVisible(bool value)
{
	if (value) {
		ClearFlag(Flags, eObjectFlags::NotProbeVisible);
	}
	else {
		SetFlag(Flags, eObjectFlags::NotProbeVisible);
	}

	for (ObjectID attached_id : AttachedNodes) {
		Object* attached_object = gObjectManager->GetObject(attached_id);
		if (attached_object != nullptr) {
			attached_object->SetProbeVisible(value);
		}
	}
}

void Object::SetHidden(bool value)
{
	if (value) {
		SetFlag(Flags, eObjectFlags::Hidden);
	}
	else {
		ClearFlag(Flags, eObjectFlags::Hidden);
	}

	for (ObjectID attached_id : AttachedNodes) {
		Object* attached_object = gObjectManager->GetObject(attached_id);
		if (attached_object != nullptr) {
			attached_object->SetHidden(value);
		}
	}
}

void Object::SetProbeVolume(bool value)
{
	if (value) {
		SetTag(eObjectTag::ProbeVolume);
	}
	else {
		ClearTag(eObjectTag::ProbeVolume);
		ClearTag(eObjectTag::ReflectionProbe);
	}

	ApplyVolumeState();
}

void Object::SetTrigger(bool value)
{
	if (value) {
		SetTag(eObjectTag::Trigger);
	}
	else {
		ClearTag(eObjectTag::Trigger);
	}

	ApplyVolumeState();
}

void Object::SetDecal(bool value)
{
	if (value) {
		SetTag(eObjectTag::Decal);
	}
	else {
		ClearTag(eObjectTag::Decal);
	}

	SetProbeVisible(!value);
	SetShadowCaster(!value);
}

void Object::ApplyVolumeState()
{
	const bool is_volume = IsVolume();

	// A marker is not geometry: it must not light the level, occlude it, or push probes out of itself
	SetProbeVisible(!is_volume);
	SetShadowCaster(!is_volume);

	// Deactivating a body is not enough, a static one still collides, so take it out of the world entirely
	physics::Body* body = gPhysics->GetBody(PhysicsID);

	if (body != nullptr && body->mbHasPhysicsBody) {
		// An invisible collider is the one marker that still has to be solid
		if (is_volume && GetSpecialMaterial() != eSpecialMaterial::NvBrush) {
			body->RemoveFromWorld();
		}
		else {
			body->AddToWorld();
		}
	}
}

void Object::SetReflectionProbe(bool value)
{
	SetProbeVolume(value);

	if (value) {
		SetTag(eObjectTag::ReflectionProbe);
	}
}

float32 Object::RaycastBounds(const Vec3f origin, const Vec3f direction, Vec3f& out_face)
{
	// Into the object's own space, so a rotated box is tested against the box it actually is rather than the
	// looser one around it. An affine transform is linear in the ray parameter, so the distance that comes back
	// is already in world metres.
	const Mat4f inverse_model = GetWorldMatrix().Inverse();

	const Vec4f local_origin = inverse_model * Vec4f(origin.X, origin.Y, origin.Z, 1.0f);
	const Vec4f local_direction = inverse_model * Vec4f(direction.X, direction.Y, direction.Z, 0.0f);

	const Ray ray(Vec3f(local_origin.X, local_origin.Y, local_origin.Z),
				  Vec3f(local_direction.X, local_direction.Y, local_direction.Z));

	return RayCast(ray, Bounds, out_face);
}

bool Object::ContainsPoint(const Vec3f point)
{
	const Vec4f local = GetWorldMatrix().Inverse() * Vec4f(point.X, point.Y, point.Z, 1.0f);

	return local.X >= Bounds.Min.X && local.X <= Bounds.Max.X && local.Y >= Bounds.Min.Y && local.Y <= Bounds.Max.Y &&
		   local.Z >= Bounds.Min.Z && local.Z <= Bounds.Max.Z;
}

void Object::SetCullable(bool value)
{
	if (!value) {
		SetFlag(Flags, eObjectFlags::DisableCulling);
	}
	else {
		ClearFlag(Flags, eObjectFlags::DisableCulling);
	}

	gWorldGrid->UpdateObject(this);
}


void Object::SetShadowCaster(const bool value)
{
	if (gWorldGrid != nullptr) {
		gWorldGrid->MarkObjectDirty(this);
	}

	if (value) {
		Flags |= eObjectFlags::ShadowCaster;
	}
	else {
		Flags &= ~(eObjectFlags::ShadowCaster);
	}

	if (gWorldGrid != nullptr) {
		gWorldGrid->MarkObjectDirty(this);
	}
}

void Object::SetPosition(const Vec3f position)
{
	const Vec3f delta = position - mPosition;

	Entity::SetPosition(position);
	InvalidateDrawCopies();

	// Move the object to its new tile now. Object::Update() does this as well, but it only runs for objects that are
	// being drawn, which means an object moved away from a tile that isn't visible would never be found again.
	gWorldGrid->UpdateObject(this);

	if (PhysicsID.IsInvalid() == false) {
		physics::Body* body = gPhysics->GetBody(PhysicsID);
		if (body != nullptr) {
			body->Teleport(position, mRotation);
		}
	}

	for (ObjectID attached_id : AttachedNodes) {
		gObjectManager->GetObject(attached_id)->MoveBy(delta);
	}
}

void Object::SetScale(const float scale)
{
	const float previous_scale = mScale;

	Entity::SetScale(scale);
	InvalidateDrawCopies();

	if (HasInstances()) {
		QueueInstanceRefresh();
	}

	gWorldGrid->UpdateObject(this);

	if (previous_scale > 0.0f) {
		const float ratio = scale / previous_scale;

		for (ObjectID attached_id : AttachedNodes) {
			Object* attached_object = gObjectManager->GetObject(attached_id);

			if (attached_object != nullptr) {
				attached_object->ScaleBy(ratio);
			}
		}
	}
}

void Object::SetRotation(const Quat rotation)
{
	const Quat delta = rotation * mRotation.Conjugate();

	Entity::SetRotation(rotation);
	InvalidateDrawCopies();

	gWorldGrid->UpdateObject(this);

	if (PhysicsID.IsInvalid() == false) {
		physics::Body* body = gPhysics->GetBody(PhysicsID);
		if (body != nullptr) {
			body->Teleport(mPosition, rotation);
		}
	}

	for (ObjectID attached_id : AttachedNodes) {
		Object* attached_object = gObjectManager->GetObject(attached_id);
		attached_object->SetRotation(delta * attached_object->mRotation);
	}
}


void Object::SetPhysicsEnabled(bool enabled)
{
	physics::Body* phys = gPhysics->GetBody(PhysicsID);

	if (!phys->mbHasPhysicsBody) {
		LogWarning(LC_CORE, "Object does not have physics body!");
		return;
	}

	if (enabled) {
		LogInfo("Activate physics body");
		gPhysics->pBackend->GetBodyInterface().ActivateBody(phys->GetBodyID());
		SetFlag(Flags, eObjectFlags::PhysicsEnabled);
	}
	else {
		LogInfo("Deactivate physics body");
		gPhysics->pBackend->GetBodyInterface().DeactivateBody(phys->GetBodyID());
		ClearFlag(Flags, eObjectFlags::PhysicsEnabled);
	}
}

void Object::PrintDebug() const
{
	LogInfo(LC_CORE, "Object '{}' (Id={}, Material={}) {{", Name.Get(), ID, mMaterialID);
	LogInfo(LC_CORE, "\tPos={}, Rot={}, Scale={}, DimMin={}, DimMax={}", mPosition, mRotation, mScale, Bounds.Min,
			Bounds.Max);

	physics::Body* phys = nullptr;

	if ((phys = gPhysics->GetBody(PhysicsID))) {
		bool has_body = phys->mbHasPhysicsBody;
		LogInfo(LC_CORE, "\tHasPhys?={}, Enabled?={}, Id={}, Type={}", has_body,
				HasFlag(Flags, eObjectFlags::PhysicsEnabled), phys->GetBodyID().GetIndex(),
				(phys->GetMotionType() == physics::eMotionType::Static) ? "Static" : "Dynamic");
	}

	LogInfo(LC_CORE, "\tInstances={}, ReadyToRender?={}, ShadowCaster?={}, Skinned?={}", mInstances.Size, /* */
			HasFlag(Flags, eObjectFlags::ReadyToRender),												  /* */
			HasFlag(Flags, eObjectFlags::ShadowCaster),													  /* */
			(pMesh && pMesh->VertexList.IsSkinned()));

	LogInfo(LC_CORE, "}}");

	LogInfo(LC_CORE, "Attached({}): ", AttachedNodes.Size());
	for (const ObjectID& obj_id : AttachedNodes) {
		Object* obj = gObjectManager->GetObject(obj_id);
		obj->PrintDebug();
	}
}


void Object::Destroy()
{
	if (gObjectScripts != nullptr) {
		gObjectScripts->Detach(ID);
		gObjectScripts->ClearRequiredEnterDirection(ID);
	}

	pMesh = nullptr;

	if (gPhysics->GetBody(PhysicsID) != nullptr) {
		gPhysics->DestroyBody(PhysicsID);
	}


	if (!AttachedNodes.IsEmpty()) {
		for (ObjectID& obj_id : AttachedNodes) {
			Object* obj = gObjectManager->GetObject(obj_id);

			if (obj != nullptr) {
				obj->Destroy();
			}
		}
	}

	mInstances.Clear();

	ClearFlag(Flags, (eObjectFlags::ReadyToRender | eObjectFlags::PhysicsEnabled));
}


} // namespace fx
