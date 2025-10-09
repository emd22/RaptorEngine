#include "FxObject.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <FxEngine.hpp>

void FxObject::Create(const FxRef<FxPrimitiveMesh<>>& mesh, const FxRef<FxMaterial>& material)
{
    Mesh = mesh;
    Material = material;
}

bool FxObject::CheckIfReady()
{
    if (mbReadyToRender) {
        return mbReadyToRender;
    }

    // If this is a container object, check that the attached nodes are ready
    if (!AttachedNodes.IsEmpty()) {
        // If there is a mesh attached to the container as well, check it
        if (Mesh && !Mesh->IsReady) {
            return (mbReadyToRender = false);
        }

        if (Material && !Material->IsReady()) {
            return (mbReadyToRender = false);
        }

        for (FxRef<FxObject>& object : AttachedNodes) {
            if (!object->CheckIfReady()) {
                return (mbReadyToRender = false);
            }
        }

        return (mbReadyToRender = true);
    }

    if (!Mesh) {
        FX_BREAKPOINT;
    }

    if (!Material || !Material->IsReady()) {
        return (mbReadyToRender = false);
    }

    // This is not a container object, just check that the mesh is loaded
    if (!Mesh || !Mesh->IsReady.load()) {
        return (mbReadyToRender = false);
    }

    return (mbReadyToRender = true);
}


void FxObject::PhysicsObjectCreate(FxPhysicsObject::PhysicsFlags flags, FxPhysicsObject::PhysicsType type,
                                   const FxPhysicsProperties& properties)
{
    Dimensions = Mesh->VertexList.CalculateDimensionsFromPositions();

    FxVec3f scaled_dimensions = Dimensions * (mScale * 0.5);

    Physics.CreatePhysicsBody(scaled_dimensions, mPosition, flags, type, properties);
    mbPhysicsEnabled = gPhysics->GetBodyInterface().IsActive(Physics.GetBodyId());
}


void FxObject::Render(const FxCamera& camera)
{
    RxFrameData* frame = gRenderer->GetFrame();
    //
    //    if (!Material || !CheckIfReady()) {
    //        return;
    //    }

    if (Material && !Material->IsBuilt) {
        Material->Build();
        return;
    }

    FxMat4f VP = camera.VPMatrix;
    FxMat4f MVP = GetModelMatrix() * VP;

    memcpy(mUbo.MvpMatrix.RawData, MVP.RawData, sizeof(FxMat4f));

    frame->SubmitUbo(mUbo);

    FxDrawPushConstants push_constants {};
    memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));

    // Copy the normal matrix to the vertex shader
    GetNormalMatrix().CopyAsMat3To(push_constants.NormalMatrix);

    // memcpy(push_constants.NormalMatrix, , sizeof(FxMat4f));

    if (Material) {
        push_constants.MaterialIndex = Material->GetMaterialIndex();
    }

    // mModel->Render(*mMaterial->Pipeline);

    if (Material && CheckIfReady()) {
        //        VkDescriptorSet sets_to_bind[] = {
        ////            gRenderer->CurrentGPass->DescriptorSet.Set,
        //
        //            Material->mDescriptorSet.Set
        //            Material->
        //        };
        //        RxDescriptorSet::BindMultiple(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        //        *Material->Pipeline, sets_to_bind, sizeof(sets_to_bind) / sizeof(sets_to_bind[0]));

        vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, Material->Pipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(push_constants), &push_constants);

        RenderMesh();
    }

    // If there are no attached nodes, break early
    if (AttachedNodes.IsEmpty()) {
        return;
    }

    // If there are attached objects, then iterate through each and render each
    // This will use the MVP and uniforms from above
    for (FxRef<FxObject>& obj : AttachedNodes) {
        if (!obj->Material) {
            continue;
        }

        if (!obj->Material->IsBuilt) {
            obj->Material->Build();
        }

        //        VkDescriptorSet sets_to_bind[] = {
        //            gRenderer->CurrentGPass->DescriptorSet.Set,
        //            obj->Material->mDescriptorSet.Set,
        //            obj->Material->mMaterialPropertiesDS.Set

        //        };


        //        const uint32 num_sets = FxSizeofArray(sets_to_bind);
        //        const uint32 properties_offset = static_cast<uint32>(obj->Material->mMaterialPropertiesIndex *
        //        sizeof(FxMaterialProperties));

        //        uint32 dynamic_offsets[] = {
        //            0,
        //            properties_offset
        //        };

        //        RxCommandBuffer& cmd = frame->CommandBuffer;
        //
        //        RxDescriptorSet::BindMultipleOffset(
        //            cmd,
        //            VK_PIPELINE_BIND_POINT_GRAPHICS,
        //            *obj->Material->Pipeline,
        //            FxMakeSlice(sets_to_bind, num_sets),
        //            FxMakeSlice(dynamic_offsets, num_sets)
        //        );
        //
        //        RxDescriptorSet::BindMultiple(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        //        *obj->Material->Pipeline, sets_to_bind, sizeof(sets_to_bind) / sizeof(sets_to_bind[0]));

        vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, obj->Material->Pipeline->Layout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);

        obj->RenderMesh();
    }
}


void FxObject::RenderMesh()
{
    if (!CheckIfReady()) {
        return;
    }

    if (!Material) {
        return;
    }

    RxFrameData* frame = gRenderer->GetFrame();

    RxCommandBuffer& cmd = frame->CommandBuffer;

    Material->Bind(&cmd);

    if (Mesh) {
        Mesh->Render(cmd, *Material->Pipeline);
    }
}

void FxObject::Update()
{
    if (!mbPhysicsEnabled) {
        return;
    }

    if (mbPhysicsTransformOutOfDate) {
        Physics.Teleport(mPosition, mRotation);
        mbPhysicsTransformOutOfDate = false;
    }

    SyncObjectWithPhysics();
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
    if (Physics.mbHasPhysicsBody) {
        if (enabled) {
            FxLogDebug("Activate body");
            gPhysics->GetBodyInterface().ActivateBody(Physics.GetBodyId());
        }
        else {
            FxLogDebug("Deactivate body");
            gPhysics->GetBodyInterface().DeactivateBody(Physics.GetBodyId());
        }
    }


    mbPhysicsEnabled = enabled;
}


void FxObject::Destroy()
{
    if (Mesh) {
        Mesh->Destroy();
    }
    if (Material) {
        Material->Destroy();
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
