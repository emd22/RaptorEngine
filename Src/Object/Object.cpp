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

	if (bIsAddedToWorld && !ID.IsInvalid() && pMesh.IsValid()) {
		gWorld->NotifyObjectMaterialChanged(ID);
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
	if (!pCurrentAnimation && Animations.Size > 0) {
		pCurrentAnimation = &Animations[0];
	}

	if (!pCurrentAnimation || !pSkeleton) {
		return;
	}


	if (AnimationTime >= pCurrentAnimation->Duration) {
		AnimationTime = 0.0f;
	}
	else if (AnimationTime < 0.0001f) {
		AnimationTime = pCurrentAnimation->Duration;
	}

	pSkeleton->EvaluatePose(*pCurrentAnimation, AnimationTime);

	AnimationTime += 0.01f;

	gGraphics->BoneBuffer.Rewind();
	gGraphics->BoneBuffer.CopyFrom(pSkeleton->SkinningMatrices.pData, pSkeleton->SkinningMatrices.Size * sizeof(Mat4f));
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


void Object::RenderShallow(const Camera& camera, renderer::Pipeline* pipeline)
{
	UpdateIfOutOfDate();

	if (!CheckIfReady(true)) {
		return;
	}

	Assert(pipeline != nullptr);

	FrameData* frame = gGraphics->GetFrame();
	// Material* material = gMaterialManager->GetMaterial(mMaterialID);

	// if (!pipeline) {
	// 	pipeline = &material->GetPipeline();
	// }


	// if (pipeline->Name == ePipelineName::Unlit) {
	// 	Assert(material->IsAlbedoOnly());
	// }
	// else if (pipeline->Name == ePipelineName::UnlitNormalMaps) {
	// 	Assert(!material->IsAlbedoOnly());
	// }

	DrawPushConstants push_constants { .TargetSize = { gGraphics->Swapchain.Extent.X, gGraphics->Swapchain.Extent.Y } };
	push_constants.ObjectId = ID.GetID();
	push_constants.MaterialIndex = mMaterialID.GetID();
	push_constants.TileColumns = gGraphics->pRenderer->GetLightTileColumns();

	// Probe capture faces have no matching SSAO data; flag the shader to use ssao=1.
	if (gProbeManager != nullptr && gProbeManager->IsCapturePending()) {
		push_constants.Flags |= 0x01;
	}
	memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(mObjectLayer).RawData, sizeof(Mat4f));

	gGraphics->SubmitPushConstants(frame->CmdBuffer, *pipeline, eShaderType::Vertex | eShaderType::Pixel,
								   push_constants);

	RenderMesh(pipeline);
}


void Object::RenderPrimitive(const CommandBuffer& cmd)
{
	if (pMesh && CheckIfReady(false)) {
		pMesh->Render(cmd, (mInstanceSlotsInUse + 1));
	}
}

void Object::RenderMesh(renderer::Pipeline* pipeline)
{
	FrameData* frame = gGraphics->GetFrame();
	CommandBuffer& cmd = frame->CmdBuffer;

	Material* mat = gMaterialManager->GetMaterial(mMaterialID);

	// If there was an error binding the object material, bind the null material.
	if (!gMaterialManager->BindWithPipeline(cmd, *pipeline, mMaterialID)) {
		gMaterialManager->BindWithPipeline(cmd, *pipeline, MaterialID::scNull);
	}

	if (pMesh) {
		pMesh->Render(cmd, (mInstanceSlotsInUse + 1)); // + 1 for source object
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

		// The transformation has changed via physics, we should tell the worldgrid
		if (mbMatrixOutOfDate) {
			gWorldGrid->UpdateObject(ID);
		}
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
	if ((!mPosition.IsCloseTo(phys->GetPosition()) || !mRotation.IsCloseTo(phys->GetRotation()))) {
		mPosition = phys->GetPosition();
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

void Object::SetPosition(const Vec3f& position)
{
	Entity::SetPosition(position);

	if (PhysicsID.IsInvalid() == false) {
		physics::Body* body = gPhysics->GetBody(PhysicsID);
		if (body != nullptr) {
			body->Teleport(position, mRotation);
		}
	}
}

void Object::SetRotation(const Quat& rotation)
{
	Entity::SetRotation(rotation);

	if (PhysicsID.IsInvalid() == false) {
		physics::Body* body = gPhysics->GetBody(PhysicsID);
		if (body != nullptr) {
			body->Teleport(mPosition, rotation);
		}
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
