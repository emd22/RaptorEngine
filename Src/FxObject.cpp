#include "FxObject.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyCreationSettings.h>
#include <ThirdParty/Jolt/Physics/Body/MotionType.h>
#include <ThirdParty/Jolt/Physics/Collision/Shape/BoxShape.h>
#include <ThirdParty/Jolt/Physics/EActivation.h>

#include <FxEngine.hpp>

void FxObject::Create(FxRef<FxPrimitiveMesh<>>& mesh, FxRef<FxMaterial>& material)
{
    Mesh = mesh;
    Material = material;
}

bool FxObject::CheckIfReady()
{
    if (mReadyToRender) {
        return mReadyToRender;
    }

    // If this is a container object, check that the attached nodes are ready
    if (!AttachedNodes.IsEmpty()) {
        // If there is a mesh attached to the container as well, check it
        if (Mesh && !Mesh->IsReady) {
            return (mReadyToRender = false);
        }

        if (Material && !Material->IsReady()) {
            return (mReadyToRender = false);
        }

        for (FxRef<FxObject>& object : AttachedNodes) {
            if (!object->CheckIfReady()) {
                return (mReadyToRender = false);
            }
        }

        return (mReadyToRender = true);
    }

    if (!Mesh) {
        FX_BREAKPOINT;
    }

    if (!Material || !Material->IsReady()) {
        return (mReadyToRender = false);
    }

    // This is not a container object, just check that the mesh is loaded
    if (!Mesh || !Mesh->IsReady.load()) {
        return (mReadyToRender = false);
    }

    return (mReadyToRender = true);
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
    FxMat4f MVP = mModelMatrix * VP;

    memcpy(mUbo.MvpMatrix.RawData, MVP.RawData, sizeof(FxMat4f));

    frame->SubmitUbo(mUbo);

    FxDrawPushConstants push_constants {};
    memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));
    memcpy(push_constants.ModelMatrix, mModelMatrix.RawData, sizeof(FxMat4f));

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

void FxObject::CreatePhysicsBody(FxObject::PhysicsFlags flags, FxObject::PhysicsType type)
{
    if (mbHasPhysicsBody) {
        FxLogWarning("Attempting to create physics body when one is already created!");
        return;
    }

    JPH::EActivation activation_mode = JPH::EActivation::Activate;

    if (flags & FxObject::PF_CreateInactive) {
        activation_mode = JPH::EActivation::DontActivate;
    }

    JPH::EMotionType motion_type = JPH::EMotionType::Static;

    FxPhysicsLayer::Type object_layer = FxPhysicsLayer::Static;

    switch (type) {
    case FxObject::PhysicsType::Static:
        motion_type = JPH::EMotionType::Static;
        object_layer = FxPhysicsLayer::Static;
        break;
    case FxObject::PhysicsType::Dynamic:
        motion_type = JPH::EMotionType::Dynamic;
        object_layer = FxPhysicsLayer::Dynamic;
        break;
    default:
        break;
    }

    JPH::RVec3 box_position;
    mPosition.ToJoltVec3(box_position);

    JPH::RVec3 box_dimensions;
    Dimensions.ToJoltVec3(box_dimensions);

    JPH::BodyCreationSettings body_settings(new JPH::BoxShape(box_dimensions), box_position, JPH::Quat::sIdentity(),
                                            motion_type, object_layer);

    gPhysics->PhysicsSystem.GetBodyInterface().CreateAndAddBody(body_settings, activation_mode);
}

void FxObject::DestroyPhysicsBody()
{
    if (!mbHasPhysicsBody) {
        return;
    }

    gPhysics->PhysicsSystem.GetBodyInterface().DestroyBody(mPhysicsBodyId);
}
