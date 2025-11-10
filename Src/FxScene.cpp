#include "FxScene.hpp"

#include <FxEngine.hpp>

void FxScene::Create()
{
    mObjects.Create(32);
    mLights.Create(32);
}

void FxScene::Attach(const FxRef<FxObject>& object) { mObjects.Insert(object); }
void FxScene::Attach(const FxRef<FxLight>& light) { mLights.Insert(light); }

void FxScene::Render()
{
    const FxCamera& camera = *mpCurrentCamera;

    // const FxRef<RxDeferredRenderer>& deferred_renderer = gRenderer->DeferredRenderer;

    // Render Geometry
    // deferred_renderer->pGeometryPipeline->Bind(gRenderer->GetFrame()->CommandBuffer);

    for (const FxRef<FxObject>& obj : mObjects) {
        obj->Update();
        obj->Render(camera);
    }

    // Render lights
    gRenderer->BeginLighting();

    for (const FxRef<FxLight>& light : mLights) {
        light->Render(camera);
    }
}
void FxScene::Destroy()
{
    mObjects.Destroy();
    mLights.Destroy();
}
