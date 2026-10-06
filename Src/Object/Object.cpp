#include "Object.hpp"


#include <Core/RefUtil.hpp>
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
#include <Renderer/PipelineCache.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <World.hpp>
#include <algorithm>
#include <utility>
#include <vector>

namespace fx {

using namespace renderer;

void Object::SetMaterial(const MaterialID& id)
{
	if (mMaterialID.GetID() == id.GetID()) {
		return;
	}

	mMaterialID = id;

	// if (bIsAddedToWorld && !ID.IsInvalid() && pMesh.IsValid()) {
	// 	gWorld->NotifyObjectMaterialChanged(ID);
	// }
}

void Object::SetMesh(const Ref<PrimitiveMesh>& mesh)
{
	pMesh = mesh;

	if (pMesh.IsValid()) {
		Flags |= eObjectFlags::HasMesh;
	}
	else {
		Flags &= ~eObjectFlags::HasMesh;
	}

	if (pMesh.IsValid() && pMesh->IsSkinned()) {
		Flags |= eObjectFlags::Skinned;
	}
	else {
		Flags &= ~eObjectFlags::Skinned;
	}
}

void Object::Create(const Ref<PrimitiveMesh>& mesh, const MaterialID& material)
{
	SetMesh(mesh);

	// Directly set the material to avoid the ol' `SetMaterial` curse
	mMaterialID = material;
}

bool Object::CheckIfReady(bool require_material)
{
	if (HasFlag(Flags, eObjectFlags::ReadyToRender)) {
		return true;
	}

	// This is not a container object, just check that the mesh is loaded
	if (!pMesh || !pMesh->IsReady()) {
		ClearFlag(Flags, eObjectFlags::ReadyToRender);
		return false;
	}

	Material* material = gMaterialManager->GetMaterial(mMaterialID);
	if (material == nullptr) {
		return false;
	}

	// Check material is ready
	if (!material->IsReadyToCheck()) {
		return false;
	}

	SetFlag(Flags, eObjectFlags::ReadyToRender);
	LogInfo(LC_RENDER, "Object {} is now ready to render.", Name.Get());

	FinalizeWhenReady();

	return true;
}


void Object::FinalizeWhenReady()
{
	if (!ParentID.IsNull()) {
		Object* parent_object = gObjectManager->GetObject(ParentID);
		parent_object->Bounds.Add(Bounds);
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
		SetPhysicsEnabled(rx_physics_is_active(gPhysics->pBackend->pWorld, phys->GetBodyID().Id) != 0);
	}
}


void Object::UpdateAnimation()
{
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
}

struct SkeletonCloneMap
{
	std::vector<std::pair<const Skeleton*, Ref<Skeleton>>> Entries;
};

Object* Object::CloneNode(const std::string& name, SkeletonCloneMap& skeletons) const
{
	Object* clone = gObjectManager->NewObject(name, mMaterialID, Tags);

	clone->SetMesh(pMesh);
	clone->Bounds = Bounds;
	clone->mPosition = mPosition;
	clone->mRotation = mRotation;
	clone->mScale = mScale;
	clone->mObjectLayer = mObjectLayer;

	clone->Flags = Flags;
	ClearFlag(clone->Flags, (eObjectFlags::ReadyToRender | eObjectFlags::IsInstance | eObjectFlags::PhysicsEnabled));
	SetFlag(clone->Flags, eObjectFlags::SharedMesh);

	clone->MarkTransformOutOfDate();

	if (pSkeleton.IsValid()) {
		const Skeleton* key = &(*pSkeleton);

		auto found = std::find_if(skeletons.Entries.begin(), skeletons.Entries.end(),
								  [key](const auto& entry) { return entry.first == key; });

		if (found == skeletons.Entries.end()) {
			skeletons.Entries.emplace_back(key, Skeleton::CreateInstance(pSkeleton));
			found = skeletons.Entries.end() - 1;
		}

		clone->pSkeleton = found->second;
	}

	if (!AttachedNodes.IsEmpty()) {
		for (const ObjectID& child_id : AttachedNodes) {
			const Object* child = gObjectManager->GetObject(child_id);

			if (child == nullptr) {
				continue;
			}

			Object* child_clone = child->CloneNode(std::string(child->Name.Get()), skeletons);
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

void Object::RenderShallow(const Camera& camera, renderer::Pipeline* pipeline)
{
	UpdateIfOutOfDate();

	if (!CheckIfReady(true) || !HasBonesForDraw()) {
		return;
	}

	Assert(pipeline != nullptr);

	FrameData* frame = gGraphics->GetFrame();

	DrawPushConstants push_constants { .TargetSize = { gGraphics->Swapchain.Extent.X, gGraphics->Swapchain.Extent.Y } };
	push_constants.ObjectId = ID.GetID();

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
	if (!pMesh || !CheckIfReady(false) || !HasBonesForDraw()) {
		return;
	}

	pMesh->Render(cmd, (mInstanceSlotsInUse + 1));
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

	// + 1 for source object
	pMesh->Render(cmd, (mInstanceSlotsInUse + 1));
}

bool Object::CanBeFrustumCulled() const
{
	const RxCullInputs inputs = {
		.cullable = IsCullable(),
		.has_mesh = pMesh.IsValid(),
		.world_layer = (mObjectLayer == eObjectLayer::WorldLayer),
		.skinned = IsSkinned(),
		.is_instance = HasFlag(Flags, eObjectFlags::IsInstance),
		.physics_enabled = HasFlag(Flags, eObjectFlags::PhysicsEnabled),
		.instance_slots_in_use = mInstanceSlotsInUse,
	};

	return rx_object_can_be_frustum_culled(&inputs, &Bounds.Min.mData[0], &Bounds.Max.mData[0]) != 0;
}

bool Object::IsOutsideFrustum(const Frustum& frustum, uint32 plane_mask)
{
	if (!CanBeFrustumCulled()) {
		return false;
	}

	return !frustum.IntersectsOBB(GetWorldOBB(), plane_mask);
}

void Object::Update()
{
	if (HasFlag(Flags, eObjectFlags::PhysicsEnabled)) {
		physics::Body* phys = gPhysics->GetBody(PhysicsID);

		if (IsPhysicsTransformOutOfDate()) {
			phys->Teleport(mPosition, mRotation);
			SetPhysicsTransformOutOfDate(false);
		}

		SyncObjectWithPhysics(phys);
	}

	// The transformation has changed, we should tell the worldgrid
	if (IsMatrixOutOfDate()) {
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

float32 Object::GetDirectionScale(const Vec3f& direction)
{
	return rx_object_direction_scale(&Bounds.Min.mData[0], &Bounds.Max.mData[0], mScale, &direction.mData[0]);
}


void Object::AttachObject(const ObjectID& attach_id)
{
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

void Object::SetProbeVolume(bool value)
{
	if (value) {
		SetTag(eObjectTag::ProbeVolume);
	}
	else {
		ClearTag(eObjectTag::ProbeVolume);
		ClearTag(eObjectTag::ReflectionProbe);
	}

	// A marker is not geometry: it must not light the level, occlude it, or push probes out of itself
	SetProbeVisible(!value);
	SetShadowCaster(!value);

	// Deactivating a body is not enough, a static one still collides, so take it out of the world entirely
	physics::Body* body = gPhysics->GetBody(PhysicsID);

	if (body != nullptr && body->mbHasPhysicsBody) {
		if (value) {
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

float32 Object::RaycastBounds(const Vec3f& origin, const Vec3f& direction, Vec3f& out_face)
{
	const Mat4f& model = GetWorldMatrix();

	return rx_object_raycast_bounds(&Bounds.Min.mData[0], &Bounds.Max.mData[0], &model.Rows[0].mData[0],
									&origin.mData[0], &direction.mData[0], &out_face.mData[0]);
}

bool Object::ContainsPoint(const Vec3f& point)
{
	const Mat4f& model = GetWorldMatrix();

	return rx_object_contains_point(&Bounds.Min.mData[0], &Bounds.Max.mData[0], &model.Rows[0].mData[0],
									&point.mData[0]) != 0;
}

void Object::SetBounds(const AABB& bounds)
{
	Bounds = bounds;

	Object* node = this;

	while (node != nullptr) {
		gWorldGrid->UpdateObject(node, false);

		if (node->ParentID.IsNull() || node->ParentID.IsInvalid()) {
			break;
		}

		Object* parent = gObjectManager->GetObject(node->ParentID);

		if (parent == nullptr) {
			break;
		}

		rx_object_merge_child_bounds(&parent->Bounds.Min.mData[0], &parent->Bounds.Max.mData[0],
									 &parent->GetWorldMatrix().Rows[0].mData[0], &node->Bounds.Min.mData[0],
									 &node->Bounds.Max.mData[0], &node->GetWorldMatrix().Rows[0].mData[0]);

		node = parent;
	}
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


void Object::SetPosition(const Vec3f& position)
{
	const Vec3f delta = position - mPosition;

	Entity::SetPosition(position);

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
	Entity::SetScale(scale);

	gWorldGrid->UpdateObject(this);
}

void Object::SetRotation(const Quat& rotation)
{
	const Quat delta = rotation * mRotation.Conjugate();

	Entity::SetRotation(rotation);

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
		rx_physics_activate(gPhysics->pBackend->pWorld, phys->GetBodyID().Id);
		SetFlag(Flags, eObjectFlags::PhysicsEnabled);
	}
	else {
		LogInfo("Deactivate physics body");
		rx_physics_deactivate(gPhysics->pBackend->pWorld, phys->GetBodyID().Id);
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
				HasFlag(Flags, eObjectFlags::PhysicsEnabled), (phys->GetBodyID().Id & 0x7FFFFF),
				(phys->GetMotionType() == physics::eMotionType::Static) ? "Static" : "Dynamic");
	}

	LogInfo(LC_CORE, "\tIsInstance?={}, ReadyToRender?={}, ShadowCaster?={}, Skinned?={}",
			HasFlag(Flags, eObjectFlags::IsInstance),	 /* */
			HasFlag(Flags, eObjectFlags::ReadyToRender), /* */
			HasFlag(Flags, eObjectFlags::ShadowCaster),	 /* */
			(pMesh && pMesh->IsSkinned()));

	LogInfo(LC_CORE, "}}");

	LogInfo(LC_CORE, "Attached({}): ", AttachedNodes.Size());
	for (const ObjectID& obj_id : AttachedNodes) {
		Object* obj = gObjectManager->GetObject(obj_id);
		obj->PrintDebug();
	}
}


void Object::Destroy()
{
	if (pMesh && !HasFlag(Flags, eObjectFlags::SharedMesh)) {
		pMesh->Destroy();
	}

	physics::Body* phys = nullptr;
	if ((phys = gPhysics->GetBody(PhysicsID)) != nullptr) {
		phys->DestroyPhysicsBody();
	}


	if (!AttachedNodes.IsEmpty()) {
		for (ObjectID& obj_id : AttachedNodes) {
			Object* obj = gObjectManager->GetObject(obj_id);
			obj->Destroy();
		}
	}

	ClearFlag(Flags, (eObjectFlags::ReadyToRender | eObjectFlags::IsInstance | eObjectFlags::PhysicsEnabled));
}


} // namespace fx

namespace fx {

static_assert(sizeof(AABB) == 2 * sizeof(Vec3f));
static_assert(sizeof(physics::BodyID) == sizeof(uint32));
static_assert(sizeof(MaterialID) == sizeof(uint32));
static_assert(sizeof(eObjectTag) == sizeof(uint32));
static_assert(sizeof(eObjectFlags) == sizeof(uint16));
static_assert(sizeof(eObjectLayer) == sizeof(uint32));
static_assert(offsetof(RxObjectCore, bounds_max) == sizeof(Vec3f));
static_assert(offsetof(RxObjectCore, tags) == 32);
static_assert(offsetof(RxObjectCore, layer) == 52);
static_assert(offsetof(RxObjectCore, flags) == 56);

} // namespace fx

static_assert(sizeof(fx::ObjectID) == sizeof(fx::uint32));
