#pragma once

#include "FxObjectManager.hpp"

#include <FxEntity.hpp>
#include <FxObject.hpp>
#include <Renderer/FxCamera.hpp>
#include <Renderer/FxLight.hpp>

class FxScene
{
public:
    FxScene() { Create(); }

    void Create();

    void Attach(const FxRef<FxObject>& object);
    void Attach(const FxRef<FxLightBase>& light);

    void SelectCamera(const FxRef<FxCamera>& camera) { mpCurrentCamera = camera; }
    void Render(FxCamera* shadow_camera);

    const FxPagedArray<FxRef<FxLightBase>>& GetAllLights() { return mLights; }
    const FxPagedArray<FxRef<FxObject>>& GetAllObjects() { return mObjects; }

    void Destroy();

    ~FxScene()
    {
        FxLogInfo("DESTROY SCENE");
        Destroy();
    }

public:
    FxObjectManager ObjectManager;

private:
    FxPagedArray<FxRef<FxObject>> mObjects;
    FxPagedArray<FxRef<FxLightBase>> mLights;

    FxRef<FxPerspectiveCamera> mpCurrentCamera { nullptr };
};
