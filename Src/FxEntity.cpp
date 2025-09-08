#include "FxEntity.hpp"

#include <Renderer/Renderer.hpp>
#include <Renderer/RxDeferred.hpp>

FxEntityManager& FxEntityManager::GetGlobalManager()
{
    static FxEntityManager global_manager;
    return global_manager;
}

void FxEntityManager::Create(uint32 entities_per_page) { GetGlobalManager().mEntityPool.Create(entities_per_page); }

FxRef<FxEntity> FxEntityManager::New()
{
    FxEntityManager& global_manager = GetGlobalManager();

    if (!global_manager.mEntityPool.IsInited()) {
        global_manager.Create();
    }

    return FxRef<FxEntity>::New(global_manager.mEntityPool.Insert());
}

/////////////////////////////
// Scene Object Functions
/////////////////////////////


void FxOldSceneObject::Render(const FxCamera& camera)
{
    assert(false);

    RxFrameData* frame = Renderer->GetFrame();

    if (!mMaterial) {
        return;
    }

    if (!mMaterial->IsBuilt) {
        mMaterial->Build();
    }

    VkDescriptorSet sets_to_bind[] = { //        Renderer->CurrentGPass->DescriptorSet.Set,
                                       mMaterial->mDescriptorSet.Set
    };

    FxMat4f VP = camera.VPMatrix;
    FxMat4f MVP = mModelMatrix * VP;
    MVP.FlipY();

    memcpy(mUbo.MvpMatrix.RawData, MVP.RawData, sizeof(FxMat4f));

    frame->SubmitUbo(mUbo);

    RxDescriptorSet::BindMultiple(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *mMaterial->Pipeline,
                                  sets_to_bind, sizeof(sets_to_bind) / sizeof(sets_to_bind[0]));

    FxDrawPushConstants push_constants {};
    memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(FxMat4f));
    memcpy(push_constants.ModelMatrix, mModelMatrix.RawData, sizeof(FxMat4f));

    vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, mMaterial->Pipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(push_constants), &push_constants);

    // if (mMaterial) {
    //     mMaterial->Bind(nullptr);
    // }

    mModel->Render(*mMaterial->Pipeline);
}
