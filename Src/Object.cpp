#include "Object.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <Core/RefUtil.hpp>
#include <Engine.hpp>
#include <ObjectManager.hpp>
#include <Physics/PhJolt.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/MeshUtil.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <Renderer/RenderBackend.hpp>
#include <Scene.hpp>

namespace fx {

using namespace renderer;

Object::Object() { ObjectId = gObjectManager->GenerateObjectId(); }

void Object::Create(const Ref<PrimitiveMesh>& mesh, const TSRef<Material>& material)
{
    pMesh = mesh;
    pMaterial = material;
}

bool Object::CheckIfReady()
{
    if ((Flags & eObjectFlags::ReadyToRender) != 0) {
        return true;
    }

    // If this is a container object, check that the attached nodes are ready
    if (!AttachedNodes.IsEmpty()) {
        // If there is a mesh attached to the container as well, check it
        if (pMesh) {
            if (!pMesh->bIsReady) {
                return (Flags &= ~(eObjectFlags::ReadyToRender)) != 0;
            }

            Dimensions = MeshUtil::CalculateDimensions(pMesh->GetVertices());
        }

        if (pMaterial && !pMaterial->IsReady()) {
            Flags &= ~(eObjectFlags::ReadyToRender);
            return false;
        }

        for (TSRef<Object>& object : AttachedNodes) {
            if (!object->CheckIfReady()) {
                Flags &= ~(eObjectFlags::ReadyToRender);
                return false;
            }
        }

        return (Flags |= (eObjectFlags::ReadyToRender)) != 0;
    }

    // Not a container, ensure there is a material
    if (!pMaterial || !pMaterial->IsReady()) {
        Flags &= ~(eObjectFlags::ReadyToRender);
        return false;
    }

    // This is not a container object, just check that the mesh is loaded
    if (!pMesh || !pMesh->bIsReady.load()) {
        Flags &= ~(eObjectFlags::ReadyToRender);
        return false;
    }

    // Dimensions = pMesh->GetDimensions();
    Dimensions = MeshUtil::CalculateDimensions(pMesh->GetVertices());

    if (pMesh->VertexList.IsSkinned()) {
        pMaterial->pPipeline = &gRenderer->pDeferredRenderer->PlGeometrySkinned;
    }

    return (Flags |= (eObjectFlags::ReadyToRender)) != 0;
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
                LogError("Error creating physics object");
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
                LogError("Error creating physics object");
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
    LogInfo("QUEUED PIPELINE UPDATE");

    if (pipeline == &gRenderer->pDeferredRenderer->PlUnlit) {
        LogInfo("SETTING TO TEXT THING 1");
    }

    OnLoaded(
        [pipeline, update_children](TSRef<AxBase> base_asset)
        {
            LogInfo("*** UPDATED PIPELINE");
            if (pipeline == &gRenderer->pDeferredRenderer->PlUnlit) {
                LogInfo("SETTING TO TEXT THING 2");
            }
            TSRef<Object> asset = base_asset;

            if (asset->pMaterial) {
                asset->pMaterial->pPipeline = pipeline;
            }

            if (update_children) {
                for (TSRef<Object>& node : asset->AttachedNodes) {
                    if (!node->pMaterial) {
                        continue;
                    }

                    node->pMaterial->pPipeline = pipeline;
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

void Object::MakeInstanceOf(const TSRef<Object>& source_ref)
{
    RefContext<TSRef<Object>> source_ctx(source_ref);
    Object& source = source_ctx.Get();

    AssertMsg((source.mInstanceSlots - source.mInstanceSlotsInUse) > 0,
              "Object has no instance slots remaining! Did you reserve any instances on the source object?");

    gObjectManager->FreeObjectId(ObjectId);

    mpInstanceSource = source_ref;
    Flags |= eObjectFlags::IsInstance;

    ++source.mInstanceSlotsInUse;

    ObjectId = source.ObjectId + source.mInstanceSlotsInUse;
}

void Object::ReserveInstances(uint32 num)
{
    ObjectId = gObjectManager->ReserveInstances(ObjectId, num);
    mInstanceSlots = num;
    mInstanceSlotsInUse = 0;
}


void Object::Render(const Camera& camera)
{
    FrameData* frame = gRenderer->GetFrame();

    if (pMaterial && !pMaterial->bIsBuilt) {
        pMaterial->Build();
        return;
    }

    UpdateIfOutOfDate();

    DrawPushConstants push_constants {};

    push_constants.ObjectId = ObjectId;

    memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(mObjectLayer).RawData, sizeof(Mat4f));

    if (pMaterial) {
        push_constants.MaterialIndex = pMaterial->GetMaterialIndex();
    }

    if (pMaterial && CheckIfReady()) {
        vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, pMaterial->pPipeline->Layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
                           &push_constants);

        RenderMesh();
    }

    // If there are no attached nodes, break early
    if (AttachedNodes.IsEmpty()) {
        return;
    }

    // If there are attached objects, then iterate through each and render each
    // This will use the MVP and uniforms from above
    for (TSRef<Object>& obj : AttachedNodes) {
        if (!obj->pMaterial) {
            continue;
        }

        if (!obj->pMaterial->bIsBuilt) {
            obj->pMaterial->Build();
        }

        obj->UpdateIfOutOfDate();

        push_constants.ObjectId = ObjectId;
        push_constants.MaterialIndex = obj->pMaterial->GetMaterialIndex();

        vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, obj->pMaterial->pPipeline->Layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
                           &push_constants);

        obj->RenderMesh();
    }
}

void Object::RenderUnlit(const Camera& camera)
{
    if (!GetRenderUnlit()) {
        return;
    }

    UpdateIfOutOfDate();

    if (pMaterial && !pMaterial->bIsBuilt) {
        pMaterial->Build();
        return;
    }

    if (pMaterial && !CheckIfReady()) {
        return;
    }


    DrawPushConstants push_constants {};
    memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(GetObjectLayer()).RawData, sizeof(Mat4f));

    CommandBuffer& cmd = gRenderer->GetFrame()->CommandBuffer;
    Pipeline* pipeline = &gRenderer->pDeferredRenderer->PlUnlit;
    if (pMaterial && pMaterial->pPipeline) {
        pipeline = pMaterial->pPipeline;
    }

    pipeline->Bind(cmd);

    if (pMaterial) {
        push_constants.ObjectId = ObjectId;
        push_constants.MaterialIndex = pMaterial->GetMaterialIndex();

        vkCmdPushConstants(cmd.Get(), pipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push_constants), &push_constants);


        bool material_bound = pMaterial->BindWithPipeline(cmd, *pipeline, false);
        if (!material_bound) {
            LogWarning("Material not bound!");
            return;
        }

        gObjectManager->mObjectBufferDS.BindWithOffset(2, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline,
                                                       gObjectManager->GetBaseOffset());

        RenderPrimitive(cmd);
    }


    gObjectManager->mObjectBufferDS.BindWithOffset(2, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline,
                                                   gObjectManager->GetBaseOffset());

    if (AttachedNodes.IsEmpty()) {
        return;
    }


    for (TSRef<Object>& obj : AttachedNodes) {
        if (!obj->pMaterial) {
            continue;
        }

        obj->Update();

        obj->UpdateIfOutOfDate();

        if (obj->pMaterial && !obj->pMaterial->bIsBuilt) {
            obj->pMaterial->Build();
            return;
        }

        if (!obj->CheckIfReady()) {
            return;
        }

        push_constants.ObjectId = ObjectId;
        push_constants.MaterialIndex = obj->pMaterial->GetMaterialIndex();

        vkCmdPushConstants(cmd.Get(), pipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push_constants), &push_constants);


        bool material_bound = obj->pMaterial->BindWithPipeline(cmd, *pipeline, false);
        if (!material_bound) {
            LogWarning("Material not bound!");
            return;
        }

        obj->RenderPrimitive(cmd);
    }
}


void Object::RenderPrimitive(const CommandBuffer& cmd)
{
    if (pMesh) {
        pMesh->Render(cmd, (mInstanceSlotsInUse + 1));
    }

    if (AttachedNodes.IsEmpty()) {
        return;
    }

    for (const TSRef<Object>& node : AttachedNodes) {
        if (node->pMesh) {
            node->pMesh->Render(cmd, (node->mInstanceSlotsInUse + 1)); // + 1 for source object!
        }
    }
}

void Object::RenderMesh()
{
    if (!CheckIfReady()) {
        return;
    }

    if (!pMaterial) {
        return;
    }

    FrameData* frame = gRenderer->GetFrame();

    CommandBuffer& cmd = frame->CommandBuffer;

    pMaterial->Bind(&cmd);

    gObjectManager->mObjectBufferDS.BindWithOffset(2, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pMaterial->pPipeline,
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

    if (pScript.IsValid()) {
        pScript->CallTickProc();
    }
}

void Object::SetScriptVars()
{
    if (!pScript.IsValid()) {
        return;
    }

    pScript->SetGlobal(HashStr32("OBJECTID"), script::FoxValue(static_cast<int32>(ObjectId)));
    pScript->CallProc(HashStr32("ObjectInit"), {});
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

void Object::AttachObject(const TSRef<Object>& object)
{
    if (AttachedNodes.IsEmpty()) {
        AttachedNodes.Create(8);
    }

    Dimensions += object->Dimensions;

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
        SetGraphicsPipeline(&gRenderer->pDeferredRenderer->PlUnlit);
        Flags |= eObjectFlags::Unlit;
    }
    else {
        SetGraphicsPipeline(&gRenderer->pDeferredRenderer->PlGeometryWithNormalMaps);
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
        LogWarning("Object does not have physics body!");
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
    LogInfo("Object '{}' (Id={}) {{", Name.Get(), ObjectId);
    LogInfo("\tPos={}, Rot={}, Scale={}, Dim={}", mPosition, mRotation, mScale, Dimensions);

    PhObject* phys = nullptr;

    if (pScene && (phys = pScene->GetPhysicsObject(PhysicsId))) {
        bool has_body = phys->mbHasPhysicsBody;
        LogInfo("\tHasPhys?={}, Enabled?={}, Id={}, Type={}", has_body,
                static_cast<bool>((Flags & eObjectFlags::PhysicsEnabled) != 0), phys->GetBodyId().GetIndex(),
                phys->GetMotionType() == ePhMotionType::Static ? "Static" : "Dynamic");
    }

    LogInfo("\tIsInstance?={}, ReadyToRender?={}, ShadowCaster?={}, Skinned?={}",
            static_cast<bool>((Flags & eObjectFlags::IsInstance) != 0),    /* */
            static_cast<bool>((Flags & eObjectFlags::ReadyToRender) != 0), /* */
            static_cast<bool>((Flags & eObjectFlags::ShadowCaster) != 0),  /* */
            (pMesh && pMesh->VertexList.IsSkinned()));

    LogInfo("}}");
}


void Object::Destroy()
{
    if (pMesh) {
        pMesh->Destroy();
    }
    if (pMaterial) {
        pMaterial->Destroy();
    }

    PhObject* phys = nullptr;
    if (pScene && (phys = pScene->GetPhysicsObject(PhysicsId))) {
        phys->DestroyPhysicsBody();
    }

    if (!AttachedNodes.IsEmpty()) {
        for (TSRef<Object>& obj : AttachedNodes) {
            obj->Destroy();
        }
    }

    Flags &= ~(eObjectFlags::ReadyToRender | eObjectFlags::IsInstance | eObjectFlags::PhysicsEnabled);

    bIsUploadedToGpu = false;
}

Object::~Object() { Destroy(); }

} // namespace fx
