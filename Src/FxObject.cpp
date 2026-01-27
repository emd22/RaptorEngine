#include "FxObject.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <FxEngine.hpp>
#include <FxObjectManager.hpp>
#include <FxScene.hpp>
#include <Physics/PhJolt.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>
#include <Renderer/RxRenderBackend.hpp>

FxObject::FxObject() { ObjectId = gObjectManager->GenerateObjectId(); }

void FxObject::Create(const FxRef<FxPrimitiveMesh<>>& mesh, const FxRef<FxMaterial>& material)
{
    pMesh = mesh;
    pMaterial = material;
}

bool FxObject::CheckIfReady()
{
    if (mbReadyToRender) {
        return mbReadyToRender;
    }

    // If this is a container object, check that the attached nodes are ready
    if (!AttachedNodes.IsEmpty()) {
        // If there is a mesh attached to the container as well, check it
        if (pMesh && !pMesh->IsReady) {
            return (mbReadyToRender = false);
        }

        if (pMaterial && !pMaterial->IsReady()) {
            return (mbReadyToRender = false);
        }

        for (FxRef<FxObject>& object : AttachedNodes) {
            if (!object->CheckIfReady()) {
                return (mbReadyToRender = false);
            }
        }

        return (mbReadyToRender = true);
    }

    if (!pMesh) {
        FX_BREAKPOINT;
    }

    if (!pMaterial || !pMaterial->IsReady()) {
        return (mbReadyToRender = false);
    }

    // This is not a container object, just check that the mesh is loaded
    if (!pMesh || !pMesh->IsReady.load()) {
        return (mbReadyToRender = false);
    }

    return (mbReadyToRender = true);
}


void FxObject::PhysicsCreatePrimitive(PhPrimitiveType primitive_type, const FxVec3f& dimensions,
                                      PhMotionType motion_type, const PhProperties& physics_properties)
{
    Physics.CreatePrimitiveBody(primitive_type, dimensions, motion_type, physics_properties);
}


void FxObject::PhysicsCreateMesh(const FxPrimitiveMesh<>& physics_mesh, PhMotionType motion_type,
                                 const PhProperties& physics_properties)
{
    Physics.CreateMeshBody(physics_mesh, motion_type, physics_properties);
}

void FxObject::OnAttached(FxScene* scene)
{
    // When the object is attached to the scene, enable physics if the physics object is active.
    if (Physics.mbHasPhysicsBody) {
        mbPhysicsEnabled = gPhysics->GetBodyInterface().IsActive(Physics.GetBodyId());
    }
}


// void FxObject::PhysicsCreate(PhObject::Flags flags, PhMotionType moititype, const PhProperties& properties)
// {
//     Dimensions = pMesh->VertexList.CalculateDimensionsFromPositions();

//     FxVec3f scaled_dimensions = Dimensions * (mScale * 0.5);

//     Physics.CreatePhysicsBody(scaled_dimensions, mPosition, flags, type, properties);
//     mbPhysicsEnabled = gPhysics->GetBodyInterface().IsActive(Physics.GetBodyId());
// }

void FxObject::SetGraphicsPipeline(RxPipeline* pipeline, bool update_children)
{
    // TODO: replace this with a callback system
    WaitUntilLoaded();

    if (pMaterial) {
        pMaterial->pPipeline = pipeline;
    }

    if (update_children) {
        for (FxRef<FxObject>& node : AttachedNodes) {
            if (!node->pMaterial) {
                continue;
            }

            node->pMaterial->pPipeline = pipeline;
        }
    }
}

void FxObject::RenderPrimitive(const RxCommandBuffer& cmd, const RxPipeline& pipeline)
{
    if (pMesh) {
        pMesh->Render(cmd, pipeline);
    }

    if (AttachedNodes.IsEmpty()) {
        return;
    }

    for (const FxRef<FxObject>& node : AttachedNodes) {
        if (node->pMesh) {
            node->pMesh->Render(cmd, pipeline);
        }
    }
}


void FxObject::Render(const FxCamera& camera)
{
    RxFrameData* frame = gRenderer->GetFrame();

    if (pMaterial && !pMaterial->bIsBuilt) {
        pMaterial->Build();
        return;
    }

    UpdateIfOutOfDate();

    FxDrawPushConstants push_constants {};

    push_constants.ObjectId = ObjectId;

    memcpy(push_constants.VPMatrix, camera.GetCameraMatrix(mObjectLayer).RawData, sizeof(FxMat4f));

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
    for (FxRef<FxObject>& obj : AttachedNodes) {
        if (!obj->pMaterial) {
            continue;
        }

        if (!obj->pMaterial->bIsBuilt) {
            obj->pMaterial->Build();
        }

        push_constants.ObjectId = ObjectId;

        vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, obj->pMaterial->pPipeline->Layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
                           &push_constants);

        obj->RenderMesh();
    }
}


void FxObject::RenderMesh()
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
        pMesh->Render(cmd, *pMaterial->pPipeline);
    }
}

void FxObject::Update()
{
    if (mbPhysicsEnabled) {
        if (mbPhysicsTransformOutOfDate) {
            Physics.Teleport(mPosition, mRotation);
            mbPhysicsTransformOutOfDate = false;
        }

        SyncObjectWithPhysics();
    }
}


void FxObject::AttachObject(const FxRef<FxObject>& object)
{
    if (AttachedNodes.IsEmpty()) {
        AttachedNodes.Create(32);
    }

    Dimensions += object->Dimensions;

    AttachedNodes.Insert(object);
}

void FxObject::SyncObjectWithPhysics()
{
    if ((!mPosition.IsCloseTo(Physics.GetPosition()) || !mRotation.IsCloseTo(Physics.GetRotation()))) {
        mPosition = Physics.GetPosition();
        mRotation = Physics.GetRotation();

        MarkMatrixOutOfDate();
    }
}


void FxObject::SetPhysicsEnabled(bool enabled)
{
    if (!Physics.mbHasPhysicsBody) {
        return;
    }

    if (enabled) {
        FxLogDebug("Activate body");
        gPhysics->GetBodyInterface().ActivateBody(Physics.GetBodyId());
    }
    else {
        FxLogDebug("Deactivate body");
        gPhysics->GetBodyInterface().DeactivateBody(Physics.GetBodyId());
    }

    mbPhysicsEnabled = enabled;
}


void FxObject::Destroy()
{
    if (pMesh) {
        pMesh->Destroy();
    }
    if (pMaterial) {
        pMaterial->Destroy();
    }

    Physics.DestroyPhysicsBody();

    if (!AttachedNodes.IsEmpty()) {
        for (FxRef<FxObject>& obj : AttachedNodes) {
            obj->Destroy();
        }
    }

    mbReadyToRender = false;
    bIsUploadedToGpu = false;
}

FxObject::~FxObject() { Destroy(); }
