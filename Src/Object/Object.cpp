#include "Object.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

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
#include <Renderer/MeshUtil.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <World.hpp>

namespace fx {

using namespace renderer;

Object::Object(const ObjectID id) { ID = id; }

Object::Object(const ObjectID id, const MaterialID material)
{
	ID = id;
	mMaterialID = material;
}

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

MaterialID Object::GetSectionMaterial(const MeshSection& section) const
{
	if (section.Material.IsNull() || HasFlag(Flags, eObjectFlags::MaterialOverridesSections)) {
		return mMaterialID;
	}

	return section.Material;
}

void Object::SetMaterialOverridesSections(bool value)
{
	if (value) {
		SetFlag(Flags, eObjectFlags::MaterialOverridesSections);
	}
	else {
		ClearFlag(Flags, eObjectFlags::MaterialOverridesSections);
	}
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
		SetPhysicsEnabled(gPhysics->pBackend->GetBodyInterface().IsActive(phys->GetBodyID()));
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

void Object::MakeInstanceOf(const ObjectID& source_id)
{
	Object* source_obj = gObjectManager->GetObject(source_id);

	AssertMsg((source_obj->mInstanceSlots - source_obj->mInstanceSlotsInUse) > 0,
			  "Object has no instance slots remaining! Did you reserve any instances on the source object?");


	gObjectManager->DestroyObject(ID);
	Flags |= eObjectFlags::IsInstance;

	++source_obj->mInstanceSlotsInUse;

	ID = ObjectID(source_obj->ID.GetID() + source_obj->mInstanceSlotsInUse);
}

void Object::ReserveInstances(uint32 num)
{
	ID = gObjectManager->ReserveInstances(ID, num);
	mInstanceSlots = num;
	mInstanceSlotsInUse = 0;
}


void Object::RenderShallow(const Camera& camera, renderer::Pipeline* pipeline, const MeshSection* section)
{
	UpdateIfOutOfDate();

	if (!CheckIfReady(true) || !HasBonesForDraw()) {
		return;
	}

	Assert(pipeline != nullptr);

	FrameData* frame = gGraphics->GetFrame();

	DrawPushConstants push_constants { .TargetSize = { gGraphics->Swapchain.Extent.X, gGraphics->Swapchain.Extent.Y } };
	push_constants.ObjectId = ID.GetID();

	push_constants.MaterialIndex = (section != nullptr) ? GetSectionMaterial(*section).GetID() : mMaterialID.GetID();
	push_constants.TileColumns = gGraphics->pRenderer->GetLightTileColumns();
	push_constants.TileRows = gGraphics->pRenderer->GetLightTileRows();
	push_constants.BoneBase = BoneBufferBase;


	// Only the probe capture faces themselves get the capture flag. Keying it off
	// IsCapturePending() would also strip probe GI and SSAO from the main view for
	// every frame of a grid bake.
	if (gProbeManager != nullptr && gProbeManager->IsCapturingFaces()) {
		push_constants.Flags |= eDrawFlags::ProbeCapture;
	}

	if (gGraphics->bOnlyRenderProbes) {
		push_constants.Flags |= eDrawFlags::DebugIrradiance;
	}

	if (gGraphics->bRenderProbeVisibility) {
		push_constants.Flags |= eDrawFlags::DebugProbeVisibility;
	}

	// Decals are placed in the world, and the view model only lines up with the world from the camera's position
	if (mObjectLayer != eObjectLayer::WorldLayer) {
		push_constants.Flags |= eDrawFlags::NoDecals;
	}

	memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(mObjectLayer).RawData, sizeof(Mat4f));
	memcpy(push_constants.EyePosition, camera.Position.mData, sizeof(float32) * 3);

	gGraphics->SubmitPushConstants(frame->CmdBuffer, *pipeline, eShaderType::Vertex | eShaderType::Pixel,
								   push_constants);

	RenderMesh(pipeline, section);
}


void Object::RenderPrimitive(const CommandBuffer& cmd, const MeshSection* section)
{
	if (!pMesh || !CheckIfReady(false) || !HasBonesForDraw()) {
		return;
	}

	if (section != nullptr) {
		pMesh->RenderRange(cmd, section->FirstIndex, section->IndexCount, (mInstanceSlotsInUse + 1));
	}
	else {
		pMesh->Render(cmd, (mInstanceSlotsInUse + 1));
	}
}

void Object::RenderMesh(renderer::Pipeline* pipeline, const MeshSection* section)
{
	FrameData* frame = gGraphics->GetFrame();
	CommandBuffer& cmd = frame->CmdBuffer;

	const MaterialID material_id = (section != nullptr) ? GetSectionMaterial(*section) : mMaterialID;

	// If there was an error binding the object material, bind the null material.
	if (!gMaterialManager->BindWithPipeline(cmd, *pipeline, material_id)) {
		gMaterialManager->BindWithPipeline(cmd, *pipeline, MaterialID::scNull);
	}

	if (!pMesh) {
		return;
	}

	// + 1 for source object
	if (section != nullptr) {
		pMesh->RenderRange(cmd, section->FirstIndex, section->IndexCount, (mInstanceSlotsInUse + 1));
	}
	else {
		pMesh->Render(cmd, (mInstanceSlotsInUse + 1));
	}
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
	if (direction.IsCloseTo(simd::LoadFloat4(0.0f))) {
		return 0.0f;
	}

	Vec3f dir = direction.Normalize();

	Vec3f pos_extent = Bounds.Max;
	Vec3f neg_extent = -Bounds.Min;

	LogInfo("Bounds min: {}, Bounds Max: {}", Bounds.Min, Bounds.Max);

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

float32 Object::RaycastBounds(const Vec3f& origin, const Vec3f& direction, Vec3f& out_face)
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

void Object::SetRotation(const Quat& rotation)
{
	const Quat delta = rotation * mRotation.Conjugate();

	Entity::SetRotation(rotation);

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

	LogInfo(LC_CORE, "\tIsInstance?={}, ReadyToRender?={}, ShadowCaster?={}, Skinned?={}",
			HasFlag(Flags, eObjectFlags::IsInstance),	 /* */
			HasFlag(Flags, eObjectFlags::ReadyToRender), /* */
			HasFlag(Flags, eObjectFlags::ShadowCaster),	 /* */
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
	if (pMesh) {
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
