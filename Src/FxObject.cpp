#include "FxObject.hpp"

#include <Renderer/Renderer.hpp>

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
    RxFrameData* frame = Renderer->GetFrame();
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

    FxDrawPushConstants push_constants{};
    memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));
    memcpy(push_constants.ModelMatrix, mModelMatrix.RawData, sizeof(FxMat4f));

    if (Material) {
        push_constants.MaterialIndex = Material->GetMaterialIndex();
    }

    // mModel->Render(*mMaterial->Pipeline);

    if (Material && CheckIfReady()) {
//        VkDescriptorSet sets_to_bind[] = {
////            Renderer->CurrentGPass->DescriptorSet.Set,
//            
//            Material->mDescriptorSet.Set
//            Material->
//        };
//        RxDescriptorSet::BindMultiple(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *Material->Pipeline, sets_to_bind, sizeof(sets_to_bind) / sizeof(sets_to_bind[0]));

        vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, Material->Pipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);

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
//            Renderer->CurrentGPass->DescriptorSet.Set,
//            obj->Material->mDescriptorSet.Set,
//            obj->Material->mMaterialPropertiesDS.Set
            
//        };
        
        
//        const uint32 num_sets = FxSizeofArray(sets_to_bind);
//        const uint32 properties_offset = static_cast<uint32>(obj->Material->mMaterialPropertiesIndex * sizeof(FxMaterialProperties));
        
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
//        RxDescriptorSet::BindMultiple(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *obj->Material->Pipeline, sets_to_bind, sizeof(sets_to_bind) / sizeof(sets_to_bind[0]));

        vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, obj->Material->Pipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);

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
    
    RxFrameData* frame = Renderer->GetFrame();
    
    RxCommandBuffer& cmd = frame->CommandBuffer;
    
    Material->Bind(&cmd);

    if (Mesh) {
        
        Mesh->Render(cmd, *Material->Pipeline);
    }
}
