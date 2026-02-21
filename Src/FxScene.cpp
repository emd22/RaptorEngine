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

FxRef<FxObject> FxScene::FindObject(FxHash64 name_hash)
{
    for (FxRef<FxObject>& obj : mObjects) {
        if (obj->Name == name_hash) {
            return obj;
        }
    }

    return FxRef<FxObject>(nullptr);
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

    RenderUnlitObjects(camera);

    gRenderer->DoComposition(camera);
}

void FxScene::RenderUnlitObjects(const FxCamera& camera) const
{
    // gRenderer->pDeferredRenderer->PlUnlit.Bind(gRenderer->GetFrame()->CommandBuffer);
    gRenderer->BeginUnlit();


    for (const FxRef<FxObject>& obj : mObjects) {
        if (!obj->GetRenderUnlit()) {
            continue;
        }
        obj->RenderUnlit(camera);
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

        obj->Update();

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
