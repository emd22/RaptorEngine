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
#include <Renderer/MeshUtil.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <Renderer/RxGlobals.hpp>
#include <Renderer/RxRenderBackend.hpp>
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
    if (mbReadyToRender) {
        return mbReadyToRender;
    }

    // If this is a container object, check that the attached nodes are ready
    if (!AttachedNodes.IsEmpty()) {
        // If there is a mesh attached to the container as well, check it
        if (pMesh) {
            if (!pMesh->bIsReady) {
                return (mbReadyToRender = false);
            }


            Dimensions = MeshUtil::CalculateDimensions(pMesh->GetVertices());
            // Dimensions = pMesh->GetDimensions();
        }

        if (pMaterial && !pMaterial->IsReady()) {
            return (mbReadyToRender = false);
        }

        for (TSRef<Object>& object : AttachedNodes) {
            if (!object->CheckIfReady()) {
                return (mbReadyToRender = false);
            }
        }

        return (mbReadyToRender = true);
    }


    if (!pMesh) {
        FX_BREAKPOINT;
    }

    // Not a container, ensure there is a material
    if (!pMaterial || !pMaterial->IsReady()) {
        return (mbReadyToRender = false);
    }

    // This is not a container object, just check that the mesh is loaded
    if (!pMesh || !pMesh->bIsReady.load()) {
        return (mbReadyToRender = false);
    }

    // Dimensions = pMesh->GetDimensions();
    Dimensions = MeshUtil::CalculateDimensions(pMesh->GetVertices());

    if (pMesh->VertexList.IsSkinned()) {
        pMaterial->pPipeline = &gRenderer->pDeferredRenderer->PlGeometrySkinned;
    }

    return (mbReadyToRender = true);
}


void Object::PhysicsCreatePrimitive(PhPrimitiveType primitive_type, const Vec3f& dimensions, PhMotionType motion_type,
                                    const PhProperties& physics_properties)
{
    OnLoaded(
        [primitive_type, dimensions, motion_type, physics_properties](TSRef<AxBase> base_asset)
        {
            TSRef<Object> asset = base_asset;
            asset->Physics.CreatePrimitiveBody(primitive_type, dimensions, motion_type, physics_properties);
            asset->mbPhysicsTransformOutOfDate = true;
            asset->SetPhysicsEnabled(true);

            asset->PrintDebug();
        });
}


void Object::PhysicsCreateMesh(Ref<PrimitiveMesh> custom_physics_mesh, PhMotionType motion_type,
                               const PhProperties& physics_properties)
{
    OnLoaded(
        [custom_physics_mesh, motion_type, physics_properties](TSRef<AxBase> base_asset)
        {
            TSRef<Object> asset = base_asset;

            Ref<PrimitiveMesh> physics_mesh { nullptr };
            physics_mesh = custom_physics_mesh ? custom_physics_mesh : asset->pMesh;

            Assert(physics_mesh.IsValid());

            asset->Physics.CreateMeshBody(*physics_mesh, motion_type, physics_properties);
            asset->mbPhysicsTransformOutOfDate = true;
            asset->SetPhysicsEnabled(true);

            asset->PrintDebug();
        });
}

void Object::OnAttached(Scene* scene)
{
    // When the object is attached to the scene, enable physics if the physics object is active.
    if (Physics.mbHasPhysicsBody) {
        mbPhysicsEnabled = gPhysics->GetBodyInterface().IsActive(Physics.GetBodyId());
    }
}


// void Object::PhysicsCreate(PhObject::Flags flags, PhMotionType moititype, const PhProperties& properties)
// {
//     Dimensions = pMesh->VertexList.CalculateDimensionsFromPositions();

//     Vec3f scaled_dimensions = Dimensions * (mScale * 0.5);

//     Physics.CreatePhysicsBody(scaled_dimensions, mPosition, flags, type, properties);
//     mbPhysicsEnabled = gPhysics->GetBodyInterface().IsActive(Physics.GetBodyId());
// }

void Object::SetGraphicsPipeline(RxPipeline* pipeline, bool update_children)
{
    OnLoaded(
        [&pipeline, update_children](TSRef<AxBase> base_asset)
        {
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
    mbIsInstance = true;

    ++source.mInstanceSlotsInUse;

    ObjectId = source.ObjectId + source.mInstanceSlotsInUse;

    // Acquire the transformations from the source object
}

void Object::ReserveInstances(uint32 num)
{
    ObjectId = gObjectManager->ReserveInstances(ObjectId, num);
    mInstanceSlots = num;
    mInstanceSlotsInUse = 0;
}


void Object::Render(const Camera& camera)
{
    RxFrameData* frame = gRenderer->GetFrame();

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

    RxCommandBuffer& cmd = gRenderer->GetFrame()->CommandBuffer;
    RxPipeline& pipeline = gRenderer->pDeferredRenderer->PlUnlit;

    pipeline.Bind(cmd);


    if (pMaterial) {
        push_constants.ObjectId = ObjectId;
        push_constants.MaterialIndex = pMaterial->GetMaterialIndex();

        vkCmdPushConstants(cmd.Get(), pipeline.Layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push_constants), &push_constants);


        bool material_bound = pMaterial->BindWithPipeline(cmd, pipeline, false);
        if (!material_bound) {
            LogWarning("Material not bound!");
            return;
        }

        gObjectManager->mObjectBufferDS.BindWithOffset(2, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline,
                                                       gObjectManager->GetBaseOffset());

        RenderPrimitive(cmd);
    }


    gObjectManager->mObjectBufferDS.BindWithOffset(2, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline,
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

        vkCmdPushConstants(cmd.Get(), pipeline.Layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push_constants), &push_constants);


        bool material_bound = obj->pMaterial->BindWithPipeline(cmd, pipeline, false);
        if (!material_bound) {
            LogWarning("Material not bound!");
            return;
        }

        obj->RenderPrimitive(cmd);
    }
}


void Object::RenderPrimitive(const RxCommandBuffer& cmd)
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

    RxFrameData* frame = gRenderer->GetFrame();

    RxCommandBuffer& cmd = frame->CommandBuffer;

    pMaterial->Bind(&cmd);

    gObjectManager->mObjectBufferDS.BindWithOffset(2, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pMaterial->pPipeline,
                                                   gObjectManager->GetBaseOffset());

    if (pMesh) {
        pMesh->Render(cmd, (mInstanceSlotsInUse + 1)); // + 1 for source object
    }
}

void Object::Update()
{
    if (mbPhysicsEnabled) {
        if (mbPhysicsTransformOutOfDate) {
            Physics.Teleport(mPosition, mRotation);
            mbPhysicsTransformOutOfDate = false;
        }

        SyncObjectWithPhysics();
    }

    UpdateAnimation();
}


void Object::AttachObject(const TSRef<Object>& object)
{
    if (AttachedNodes.IsEmpty()) {
        AttachedNodes.Create(8);
    }

    Dimensions += object->Dimensions;

    AttachedNodes.Insert(object);
}

void Object::SyncObjectWithPhysics()
{
    if ((!mPosition.IsCloseTo(Physics.GetPosition()) || !mRotation.IsCloseTo(Physics.GetRotation()))) {
        mPosition = Physics.GetPosition();
        mRotation = Physics.GetRotation();

        MarkMatrixOutOfDate();
    }
}

void Object::SetRenderUnlit(const bool value)
{
    if (value) {
        SetGraphicsPipeline(&gRenderer->pDeferredRenderer->PlUnlit);
    }
    else {
        SetGraphicsPipeline(&gRenderer->pDeferredRenderer->PlGeometryWithNormalMaps);
    }

    mbRenderUnlit = value;
}


void Object::SetPhysicsEnabled(bool enabled)
{
    if (!Physics.mbHasPhysicsBody) {
        LogWarning("Object does not have physics body!");
        return;
    }

    if (enabled) {
        LogInfo("Activate physics body");
        gPhysics->GetBodyInterface().ActivateBody(Physics.GetBodyId());
    }
    else {
        LogInfo("Deactivate physics body");
        gPhysics->GetBodyInterface().DeactivateBody(Physics.GetBodyId());
    }

    mbPhysicsEnabled = enabled;
}

void Object::PrintDebug() const
{
    LogInfo("Object '{}' (Id={}) {{", Name.Get(), ObjectId);
    LogInfo("\tPos={}, Rot={}, Scale={}, Dim={}", mPosition, mRotation, mScale, Dimensions);
    LogInfo("\tHasPhys?={}, Enabled?={}, Type={}", Physics.mbHasPhysicsBody, mbPhysicsEnabled,
            Physics.GetMotionType() == PhMotionType::eStatic ? "Static" : "Dynamic");
    LogInfo("\tIsInstance?={}, ReadyToRender?={}, ShadowCaster?={}, Skinned?={}", mbIsInstance, mbReadyToRender,
            mbIsShadowCaster, pMesh && pMesh->VertexList.IsSkinned());
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

    Physics.DestroyPhysicsBody();

    if (!AttachedNodes.IsEmpty()) {
        for (TSRef<Object>& obj : AttachedNodes) {
            obj->Destroy();
        }
    }

    mbReadyToRender = false;
    bIsUploadedToGpu = false;
}

Object::~Object() { Destroy(); }

} // namespace fx
