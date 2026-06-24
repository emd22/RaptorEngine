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
#include <Physics/PhJolt.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/MeshUtil.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <Renderer/RenderBackend.hpp>
#include <Scene.hpp>

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
    if ((Flags & eObjectFlags::ReadyToRender) != 0) {
        return true;
    }

    // Not a container, ensure there is a material
    // if (require_material && (!pMaterial || !pMaterial->IsReady())) {
    //     Flags &= ~(eObjectFlags::ReadyToRender);
    //     return false;
    // }

    // This is not a container object, just check that the mesh is loaded
    if (!pMesh || !pMesh->bIsReady) {
        Flags &= ~(eObjectFlags::ReadyToRender);
        return false;
    }

    // Dimensions = pMesh->GetDimensions();
    Dimensions = MeshUtil::CalculateDimensions(pMesh->GetVertices());

    // TODO: Remove this
    if (require_material && pMesh->VertexList.IsSkinned()) {
        Material* material = gMaterialManager->GetMaterial(mMaterialID);
        material->SetPipeline(ePipelineName::GeometrySkinned);
    }

    Flags |= (eObjectFlags::ReadyToRender);
    return true;
}


void Object::PhysicsCreatePrimitive(ePhPrimitiveType primitive_type, const Vec3f& dimensions, ePhMotionType motion_type,
                                    const PhProperties& physics_properties)
{
    OnLoaded(
        [primitive_type, dimensions, motion_type, physics_properties](TSRef<AxBase> base_asset)
        {
            TSRef<Object> object = base_asset;

            Scene* scene = object->pScene;
            if (!scene) {
                return;
            }

            if (object->PhysicsId == PhObjectIdNull) {
                object->PhysicsId = scene->NewPhysicsObject();
            }

            PhObject* phys = scene->GetPhysicsObject(object->PhysicsId);

            if (!phys) {
                LogError(LC_PHYSICS, "Error creating physics object");
                return;
            }

            phys->CreatePrimitiveBody(primitive_type, dimensions, motion_type, physics_properties);
            object->mbPhysicsTransformOutOfDate = true;
            object->SetPhysicsEnabled(true);

            object->PrintDebug();
        });
}


void Object::PhysicsCreateMesh(Ref<PrimitiveMesh> custom_physics_mesh, ePhMotionType motion_type,
                               const PhProperties& physics_properties)
{
    OnLoaded(
        [custom_physics_mesh, motion_type, physics_properties](TSRef<AxBase> base_asset)
        {
            TSRef<Object> object = base_asset;

            Scene* scene = object->pScene;
            if (!scene) {
                return;
            }

            object->PhysicsId = scene->NewPhysicsObject();
            PhObject* phys = scene->GetPhysicsObject(object->PhysicsId);

            if (!phys) {
                LogError(LC_PHYSICS, "Error creating physics object");
                return;
            }

            Ref<PrimitiveMesh> physics_mesh { nullptr };
            physics_mesh = custom_physics_mesh ? custom_physics_mesh : object->pMesh;

            Assert(physics_mesh.IsValid());

            phys->CreateMeshBody(*physics_mesh, motion_type, physics_properties);
            object->mbPhysicsTransformOutOfDate = true;
            object->SetPhysicsEnabled(true);

            object->PrintDebug();
        });
}

void Object::OnAttached(Scene* scene)
{
    PhObject* phys = scene->GetPhysicsObject(PhysicsId);

    // When the object is attached to the scene, enable physics if the physics object is active.
    if (phys && phys->mbHasPhysicsBody) {
        SetPhysicsEnabled(gPhysics->GetBodyInterface().IsActive(phys->GetBodyId()));
    }
}


// void Object::PhysicsCreate(PhObject::Flags flags, PhMotionType moititype, const PhProperties& properties)
// {
//     Dimensions = pMesh->VertexList.CalculateDimensionsFromPositions();

//     Vec3f scaled_dimensions = Dimensions * (mScale * 0.5);

//     Physics.CreatePhysicsBody(scaled_dimensions, mPosition, flags, type, properties);
//     mbPhysicsEnabled = gPhysics->GetBodyInterface().IsActive(Physics.GetBodyId());
// }

void Object::SetGraphicsPipeline(Pipeline* pipeline, bool update_children)
{
    OnLoaded(
        [pipeline, update_children](TSRef<AxBase> base_asset)
        {
            TSRef<Object> asset = base_asset;

            const bool should_set_default = (pipeline == nullptr);


            if (!asset->GetMaterialID().IsNull()) {
                Material* material = gMaterialManager->GetMaterial(asset->mMaterialID);

                if (should_set_default) {
                    material->SetDefaultPipeline();
                }
                else {
                    material->SetPipeline(gPipelineCache->GetName(pipeline));
                }
            }

            if (update_children) {
                for (const ObjectID& attached_id : asset->AttachedNodes) {
                    Object* attached = gObjectManager->GetObject(attached_id);

                    if (!attached->GetMaterialID().IsNull()) {
                        continue;
                    }

                    Material* material = gMaterialManager->GetMaterial(attached->GetMaterialID());

                    if (should_set_default) {
                        material->SetDefaultPipeline();
                    }
                    else {
                        material->SetPipeline(gPipelineCache->GetName(pipeline));
                    }
                }
            }
        });
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
    else if (AnimationTime < 0.0001) {
        AnimationTime = pCurrentAnimation->Duration;
    }

    pSkeleton->EvaluatePose(*pCurrentAnimation, AnimationTime);

    AnimationTime += 0.01f;

    gRenderer->BoneBuffer.Rewind();
    gRenderer->BoneBuffer.CopyFrom(pSkeleton->SkinningMatrices.pData, pSkeleton->SkinningMatrices.Size * sizeof(Mat4f));
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


void Object::Render(const Camera& camera)
{
    FrameData* frame = gRenderer->GetFrame();

    MaterialID material_id = mMaterialID;

    // const bool use_null_material = mMaterialID.IsNull();
    Material* material = gMaterialManager->GetMaterial(material_id);

    // if (!use_null_material && !material->bIsBuilt) {
    //     material->Build();
    //     return;
    // }

    UpdateIfOutOfDate();

    DrawPushConstants push_constants {};
    push_constants.ObjectId = ID.GetID();
    push_constants.MaterialIndex = mMaterialID.GetID();
    memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(mObjectLayer).RawData, sizeof(Mat4f));

    if (CheckIfReady(true)) {
        gRenderer->SubmitPushConstants(frame->CmdBuffer, material->GetPipeline(),
                                       eShaderType::Vertex | eShaderType::Pixel, push_constants);

        RenderMesh();
    }

    // If there are no attached nodes, break early
    if (AttachedNodes.IsEmpty()) {
        return;
    }

    for (const ObjectID& obj_id : AttachedNodes) {
        Object* obj = gObjectManager->GetObject(obj_id);
        obj->Render(camera);
    }
}

void Object::RenderShallow(const Camera& camera)
{
    FrameData* frame = gRenderer->GetFrame();

    Material* material = gMaterialManager->GetMaterial(mMaterialID);

    UpdateIfOutOfDate();

    DrawPushConstants push_constants {};
    push_constants.ObjectId = ID.GetID();
    push_constants.MaterialIndex = mMaterialID.GetID();
    memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(mObjectLayer).RawData, sizeof(Mat4f));

    gRenderer->SubmitPushConstants(frame->CmdBuffer, material->GetPipeline(), eShaderType::Vertex | eShaderType::Pixel,
                                   push_constants);

    RenderMesh();
}

void Object::RenderUnlit(const Camera& camera)
{
    if (!GetRenderUnlit()) {
        return;
    }

    FrameData* frame = gRenderer->GetFrame();

    MaterialID material_id = MaterialID::Null;

    // bool use_null_material = mMaterialID.IsNull();
    Material* material = gMaterialManager->GetMaterial(material_id);

    // if (!use_null_material && !material->bIsBuilt) {
    //     material->Build();
    //     use_null_material = true;
    // }

    UpdateIfOutOfDate();

    DrawPushConstants push_constants {};
    push_constants.ObjectId = ID.GetID();
    push_constants.MaterialIndex = mMaterialID.GetID();
    memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(mObjectLayer).RawData, sizeof(Mat4f));


    if (!gMaterialManager->Bind(frame->CmdBuffer, mMaterialID)) {
        gMaterialManager->Bind(frame->CmdBuffer, MaterialID::Null);
    }

    Pipeline& pipeline = gPipelineCache->Request(ePipelineName::Unlit);
    pipeline.Bind(frame->CmdBuffer);

    gObjectManager->mObjectBufferDS.BindWithOffset(2, frame->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline,
                                                   gObjectManager->GetBaseOffset());

    if (CheckIfReady(true)) {
        gRenderer->SubmitPushConstants(frame->CmdBuffer, pipeline, eShaderType::Vertex | eShaderType::Pixel,
                                       push_constants);
        RenderPrimitive(frame->CmdBuffer);
    }

    if (AttachedNodes.IsEmpty()) {
        return;
    }

    for (const ObjectID& obj_id : AttachedNodes) {
        Object* obj = gObjectManager->GetObject(obj_id);
        obj->RenderUnlit(camera);
    }
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

void Object::RenderMesh()
{
    if (!CheckIfReady(true)) {
        return;
    }

    FrameData* frame = gRenderer->GetFrame();
    CommandBuffer& cmd = frame->CmdBuffer;

    MaterialID material_id = mMaterialID;


    if (!gMaterialManager->Bind(cmd, material_id)) {
        gMaterialManager->Bind(cmd, MaterialID::Null);
    }

    gObjectManager->mObjectBufferDS.BindWithOffset(2, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                   gMaterialManager->GetMaterial(material_id)->GetPipeline(),
                                                   gObjectManager->GetBaseOffset());

    if (pMesh) {
        pMesh->Render(cmd, (mInstanceSlotsInUse + 1)); // + 1 for source object
    }
}

void Object::Update()
{
    if ((Flags & eObjectFlags::PhysicsEnabled) != 0 && pScene) {
        PhObject* phys = pScene->GetPhysicsObject(PhysicsId);

        if (mbPhysicsTransformOutOfDate) {
            phys->Teleport(mPosition, mRotation);
            mbPhysicsTransformOutOfDate = false;
        }

        SyncObjectWithPhysics(phys);
    }

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

void Object::AttachObject(const ObjectID& object)
{
    if (!AttachedNodes.IsInited()) {
        AttachedNodes.Create(8);
    }

    AttachedNodes.Insert(object);
}

void Object::SyncObjectWithPhysics(PhObject* phys)
{
    if ((!mPosition.IsCloseTo(phys->GetPosition()) || !mRotation.IsCloseTo(phys->GetRotation()))) {
        mPosition = phys->GetPosition();
        mRotation = phys->GetRotation();

        MarkMatrixOutOfDate();
    }
}

void Object::SetRenderUnlit(const bool value)
{
    if (value) {
        SetGraphicsPipeline(&gPipelineCache->Request(ePipelineName::Unlit));
        Flags |= eObjectFlags::Unlit;
    }
    else {
        // Set to default graphics pipeline
        SetGraphicsPipeline(nullptr);
        Flags &= (~eObjectFlags::Unlit);
    }
}


void Object::SetPhysicsEnabled(bool enabled)
{
    if (!pScene) {
        return;
    }

    PhObject* phys = pScene->GetPhysicsObject(PhysicsId);

    if (!phys->mbHasPhysicsBody) {
        LogWarning(LC_CORE, "Object does not have physics body!");
        return;
    }

    if (enabled) {
        LogInfo("Activate physics body");
        gPhysics->GetBodyInterface().ActivateBody(phys->GetBodyId());
        Flags |= eObjectFlags::PhysicsEnabled;
    }
    else {
        LogInfo("Deactivate physics body");
        gPhysics->GetBodyInterface().DeactivateBody(phys->GetBodyId());

        Flags &= (~eObjectFlags::PhysicsEnabled);
    }
}

void Object::PrintDebug() const
{
    LogInfo(LC_CORE, "Object '{}' (Id={}, Material={}) {{", Name.Get(), ID, mMaterialID);
    LogInfo(LC_CORE, "\tPos={}, Rot={}, Scale={}, Dim={}", mPosition, mRotation, mScale, Dimensions);

    PhObject* phys = nullptr;

    if (pScene && (phys = pScene->GetPhysicsObject(PhysicsId))) {
        bool has_body = phys->mbHasPhysicsBody;
        LogInfo(LC_CORE, "\tHasPhys?={}, Enabled?={}, Id={}, Type={}", has_body,
                static_cast<bool>((Flags & eObjectFlags::PhysicsEnabled) != 0), phys->GetBodyId().GetIndex(),
                phys->GetMotionType() == ePhMotionType::Static ? "Static" : "Dynamic");
    }

    LogInfo(LC_CORE, "\tIsInstance?={}, ReadyToRender?={}, ShadowCaster?={}, Skinned?={}",
            static_cast<bool>((Flags & eObjectFlags::IsInstance) != 0),    /* */
            static_cast<bool>((Flags & eObjectFlags::ReadyToRender) != 0), /* */
            static_cast<bool>((Flags & eObjectFlags::ShadowCaster) != 0),  /* */
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

    PhObject* phys = nullptr;
    if (pScene && (phys = pScene->GetPhysicsObject(PhysicsId))) {
        phys->DestroyPhysicsBody();
    }

    if (!AttachedNodes.IsEmpty()) {
        for (ObjectID& obj_id : AttachedNodes) {
            Object* obj = gObjectManager->GetObject(obj_id);
            obj->Destroy();
        }
    }

    Flags &= ~(eObjectFlags::ReadyToRender | eObjectFlags::IsInstance | eObjectFlags::PhysicsEnabled);

    bIsUploadedToGpu = false;
}

Object::~Object() { Destroy(); }

} // namespace fx
