#include "FxEntity.hpp"

#include <Renderer/FxDeferred.hpp>

FxEntityManager& FxEntityManager::GetGlobalManager()
{
    static FxEntityManager global_manager;
    return global_manager;
}

void FxEntityManager::Create(uint32 entities_per_page)
{
    GetGlobalManager().mEntityPool.Create(entities_per_page);
}

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


void FxSceneObject::Render(const FxCamera& camera)
{
    RvkFrameData* frame = Renderer->GetFrame();

    if (!mMaterial || !mMaterial->IsBuilt) {
        mMaterial->Build();
    }

    VkDescriptorSet sets_to_bind[] = {
        Renderer->CurrentGPass->DescriptorSet.Set,
        mMaterial->mDescriptorSet.Set
    };

    Mat4f VP = camera.VPMatrix;
    Mat4f MVP = mModelMatrix * VP;

    memcpy(mUbo.MvpMatrix.RawData, MVP.RawData, sizeof(Mat4f));

    frame->SubmitUbo(mUbo);

    RvkDescriptorSet::BindMultiple(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *mMaterial->Pipeline, sets_to_bind, sizeof(sets_to_bind) / sizeof(sets_to_bind[0]));

    DrawPushConstants push_constants{};
    memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(Mat4f));
    memcpy(push_constants.ModelMatrix, mModelMatrix.RawData, sizeof(Mat4f));

    vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, mMaterial->Pipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);

    // if (mMaterial) {
    //     mMaterial->Bind(nullptr);
    // }

    mModel->Render(*mMaterial->Pipeline);
}
