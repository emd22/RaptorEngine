#include "FxScene.hpp"

#include <FxEngine.hpp>
#include <Renderer/RxRenderBackend.hpp>

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
    const FxPerspectiveCamera& camera = *mpCurrentCamera;

    // const FxRef<RxDeferredRenderer>& deferred_renderer = gRenderer->DeferredRenderer;

    // Render Geometry
    // deferred_renderer->pGeometryPipeline->Bind(gRenderer->GetFrame()->CommandBuffer);

    for (const FxRef<FxObject>& obj : mObjects) {
        obj->Update();
        obj->Render(camera);
    }

    // Render lights
    gRenderer->BeginLighting();

    for (const FxRef<FxLightBase>& light : mLights) {
        light->Render(camera, shadow_camera);
    }
}
void FxScene::Destroy()
{
    mObjects.Destroy();
    mLights.Destroy();
}
