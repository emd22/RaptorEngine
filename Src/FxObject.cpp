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
    
    if (!AttachedNodes.IsEmpty()) {
        for (FxRef<FxObject>& obj : AttachedNodes) {
            obj->Render(camera);
        }
    }

    if (!Material || !CheckIfReady()) {
        return;
    }

    if (!Material->IsBuilt) {
        Material->Build();
    }

    VkDescriptorSet sets_to_bind[] = {
        Renderer->CurrentGPass->DescriptorSet.Set,
        Material->mDescriptorSet.Set
    };

    FxMat4f VP = camera.VPMatrix;
    FxMat4f MVP = mModelMatrix * VP;
    MVP.FlipY();

    memcpy(mUbo.MvpMatrix.RawData, MVP.RawData, sizeof(FxMat4f));

    frame->SubmitUbo(mUbo);

    RxDescriptorSet::BindMultiple(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *Material->Pipeline, sets_to_bind, sizeof(sets_to_bind) / sizeof(sets_to_bind[0]));

    FxDrawPushConstants push_constants{};
    memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));
    memcpy(push_constants.ModelMatrix, mModelMatrix.RawData, sizeof(FxMat4f));

    vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, Material->Pipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);

    // mModel->Render(*mMaterial->Pipeline);
    RenderMesh();
}


void FxObject::RenderMesh()
{
    if (!CheckIfReady()) {
        return;
    }
    
    if (Mesh) {
        Mesh->Render(*Material->Pipeline);
    }
}
