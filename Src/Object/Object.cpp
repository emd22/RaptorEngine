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
#include <Renderer/MeshUtil.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <World.hpp>

namespace fx {

using namespace renderer;

Object::Object(const ObjectID& id) { ID = id; }

void Object::Create(const Ref<PrimitiveMesh>& mesh, const MaterialID& material)
{
	pMesh = mesh;
	SetMaterialID(material);
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

	// The object is now ready to render and is "full". Finalize any changes that have been made when loading.
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


void Object::PhysicsCreatePrimitive(physics::ePrimitiveType primitive_type, const Vec3f& dimensions,
									physics::eMotionType motion_type, const physics::BodyProps& physics_properties)
{
	// OnLoaded(
	//     [&]()
	//     {
	//         Scene* scene = this->pScene;
	//         if (!scene) {
	//             return;
	//         }

	//         if (this->PhysicsId == BodyIdNull) {
	//             this->PhysicsId = scene->NewPhysicsObject();
	//         }

	//         Body* phys = scene->GetPhysicsObject(this->PhysicsId);

	//         if (!phys) {
	//             LogError(LC_PHYSICS, "Error creating physics object");
	//             return;
	//         }

	//         phys->CreatePrimitiveBody(primitive_type, dimensions, motion_type, physics_properties);
	//         this->mbPhysicsTransformOutOfDate = true;
	//         this->SetPhysicsEnabled(true);

	//         this->PrintDebug();
	//     });
}


void Object::PhysicsCreateMesh(Ref<PrimitiveMesh> custom_physics_mesh, physics::eMotionType motion_type,
							   const physics::BodyProps& physics_properties)
{
	// OnLoaded(
	//     [&]()
	//     {
	//         Scene* scene = this->pScene;
	//         if (!scene) {
	//             return;
	//         }

	//         this->PhysicsId = scene->NewPhysicsObject();
	//         Body* phys = scene->GetPhysicsObject(this->PhysicsId);

	//         if (!phys) {
	//             LogError(LC_PHYSICS, "Error creating physics object");
	//             return;
	//         }

	//         Ref<PrimitiveMesh> physics_mesh { nullptr };
	//         physics_mesh = custom_physics_mesh ? custom_physics_mesh : this->pMesh;

	//         Assert(physics_mesh.IsValid());

	//         phys->CreateMeshBody(*physics_mesh, motion_type, physics_properties);
	//         this->mbPhysicsTransformOutOfDate = true;
	//         this->SetPhysicsEnabled(true);

	//         this->PrintDebug();
	//     });
}

void Object::OnAttached(World* scene)
{
	physics::Body* phys = gPhysics->GetBody(PhysicsID);

	// When the object is attached to the scene, enable physics if the physics object is active.
	if (phys && phys->mbHasPhysicsBody) {
		SetPhysicsEnabled(gPhysics->pBackend->GetBodyInterface().IsActive(phys->GetBodyId()));
	}
}


// void Object::PhysicsCreate(Body::Flags flags, PhMotionType moititype, const PhProperties& properties)
// {
//     Dimensions = pMesh->VertexList.CalculateDimensionsFromPositions();

//     Vec3f scaled_dimensions = Dimensions * (mScale * 0.5);

//     Physics.CreatePhysicsBody(scaled_dimensions, mPosition, flags, type, properties);
//     mbPhysicsEnabled = gPhysics->pBackend->GetBodyInterface().IsActive(Physics.GetBodyId());
// }


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

	// if (AttachedNodes.IsEmpty()) {
	//     return;
	// }

	// for (const TSRef<Object>& node : AttachedNodes) {
	//     if (node->pMesh && node->CheckIfReady(false)) {
	//         node->pMesh->Render(cmd, (node->mInstanceSlotsInUse + 1)); // + 1 for source object!
	//     }
	// }
}

void Object::RenderMesh(renderer::Pipeline* pipeline)
{
	FrameData* frame = gGraphics->GetFrame();
	CommandBuffer& cmd = frame->CmdBuffer;

	Material* mat = gMaterialManager->GetMaterial(mMaterialID);
	if (mat && pipeline->Name != ePipelineName::ShadowDirectional) {
		Assert(mat->GetRequiredPipeline() == pipeline->Name);
	}

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
	if (HasFlag(Flags, eObjectFlags::PhysicsEnabled) && pScene) {
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

void Object::SetScriptVars()
{
	if (!pScript.IsValid()) {
		return;
	}

	pScript->SetGlobal(HashStr32("OBJECTID"), script::FoxValue(static_cast<int32>(ID.GetID())));
}

void Object::AttachScript(const Ref<script::FoxScript>& script)
{
	pScript = script;
	SetScriptVars();
}

void Object::LoadScript(const String& path)
{
	pScript = MakeRef<script::FoxScript>(path);
	SetScriptVars();
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


void Object::SetPhysicsEnabled(bool enabled)
{
	if (!pScene) {
		return;
	}

	physics::Body* phys = gPhysics->GetBody(PhysicsID);

	if (!phys->mbHasPhysicsBody) {
		LogWarning(LC_CORE, "Object does not have physics body!");
		return;
	}

	if (enabled) {
		LogInfo("Activate physics body");
		gPhysics->pBackend->GetBodyInterface().ActivateBody(phys->GetBodyId());
		SetFlag(Flags, eObjectFlags::PhysicsEnabled);
	}
	else {
		LogInfo("Deactivate physics body");
		gPhysics->pBackend->GetBodyInterface().DeactivateBody(phys->GetBodyId());
		ClearFlag(Flags, eObjectFlags::PhysicsEnabled);
	}
}

void Object::PrintDebug() const
{
	LogInfo(LC_CORE, "Object '{}' (Id={}, Material={}) {{", Name.Get(), ID, mMaterialID);
	LogInfo(LC_CORE, "\tPos={}, Rot={}, Scale={}, DimMin={}, DimMax={}", mPosition, mRotation, mScale, Bounds.Min,
			Bounds.Max);

	physics::Body* phys = nullptr;

	if (pScene && (phys = gPhysics->GetBody(PhysicsID))) {
		bool has_body = phys->mbHasPhysicsBody;
		LogInfo(LC_CORE, "\tHasPhys?={}, Enabled?={}, Id={}, Type={}", has_body,
				HasFlag(Flags, eObjectFlags::PhysicsEnabled), phys->GetBodyId().GetIndex(),
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
	if (pScene != nullptr && (phys = gPhysics->GetBody(PhysicsID)) != nullptr) {
		phys->DestroyPhysicsBody();
	}

	pScene = nullptr;

	if (!AttachedNodes.IsEmpty()) {
		for (ObjectID& obj_id : AttachedNodes) {
			Object* obj = gObjectManager->GetObject(obj_id);
			obj->Destroy();
		}
	}

	ClearFlag(Flags, (eObjectFlags::ReadyToRender | eObjectFlags::IsInstance | eObjectFlags::PhysicsEnabled));
}


} // namespace fx
