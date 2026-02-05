#include "FxScene.hpp"

#include <FxEngine.hpp>
#include <Renderer/RxRenderBackend.hpp>
#include <Renderer/RxShadowDirectional.hpp>

void FxScene::Create()
{
    mObjects.Create(32);
    mLights.Create(32);
}

void FxScene::Attach(const FxRef<FxObject>& object)
{
    mObjects.Insert(object);
    object->OnAttached(this);
}
void FxScene::Attach(const FxRef<FxLightBase>& light)
{
    mLights.Insert(light);
    light->OnAttached(this);
}

void FxScene::Render(FxCamera* shadow_camera)
{
    FxPerspectiveCamera& camera = *mpCurrentCamera;

    gRenderer->BeginGeometry();

    for (const FxRef<FxObject>& obj : mObjects) {
        obj->Update();

        if (obj->GetRenderUnlit()) {
            continue;
        }

        obj->Render(camera);
    }

    // Render lights
    gRenderer->BeginLighting();

    for (const FxRef<FxLightBase>& light : mLights) {
        light->Render(camera, shadow_camera);
    }

    // RenderUnlitObjects(camera);

    gRenderer->DoComposition(camera);
}

void FxScene::RenderUnlitObjects(const FxCamera& camera) const
{
    RxCommandBuffer& cmd = gRenderer->GetFrame()->CommandBuffer;

    RxPipeline& pipeline = gRenderer->pDeferredRenderer->PlUnlit;

    pipeline.Bind(cmd);

    FxDrawPushConstants push_constants {};

    for (const FxRef<FxObject>& obj : mObjects) {
        if (!obj->GetRenderUnlit()) {
            continue;
        }

        pipeline.Bind(cmd);


        push_constants.ObjectId = obj->ObjectId;

        memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(obj->GetObjectLayer()).RawData, sizeof(FxMat4f));

        if (obj->pMaterial) {
            push_constants.MaterialIndex = obj->pMaterial->GetMaterialIndex();
            obj->pMaterial->BindWithPipeline(cmd, pipeline, true);
        }


        gObjectManager->mObjectBufferDS.BindWithOffset(2, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline,
                                                       gObjectManager->GetBaseOffset());

        vkCmdPushConstants(cmd.Get(), obj->pMaterial->pPipeline->Layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
                           &push_constants);

        obj->RenderPrimitive(cmd);
    }
}

void FxScene::RenderShadows(FxCamera* shadow_camera)
{
    gShadowRenderer->Begin();

    RxShadowPushConstants consts;

    memcpy(consts.CameraMatrix, gShadowRenderer->ShadowCamera.GetCameraMatrix(FxObjectLayer::eWorldLayer).RawData,
           sizeof(float32) * 16);

    for (const FxRef<FxObject>& obj : mObjects) {
        if (!obj->IsShadowCaster()) {
            continue;
        }

        consts.ObjectId = obj->ObjectId;

        vkCmdPushConstants(gRenderer->GetFrame()->CommandBuffer.Get(), gShadowRenderer->GetPipeline().Layout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RxShadowPushConstants), &consts);

        obj->RenderPrimitive(gRenderer->GetFrame()->CommandBuffer);
    }

    gShadowRenderer->End();
}

void FxScene::Destroy()
{
    mObjects.Destroy();
    mLights.Destroy();
}
