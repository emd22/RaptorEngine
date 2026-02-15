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
    void RenderShadows(FxCamera* shadow_camera);

    const FxPagedArray<FxRef<FxLightBase>>& GetAllLights() { return mLights; }
    const FxPagedArray<FxRef<FxObject>>& GetAllObjects() { return mObjects; }

    FxRef<FxObject> FindObject(FxHash64 name_hash);

    void Destroy();

    ~FxScene() { Destroy(); }

private:
    void RenderUnlitObjects(const FxCamera& camera) const;

public:
    FxObjectManager ObjectManager;

    FxName Name = "(unnamed)";

private:
    FxPagedArray<FxRef<FxObject>> mObjects;
    FxPagedArray<FxRef<FxLightBase>> mLights;

    FxRef<FxPerspectiveCamera> mpCurrentCamera { nullptr };
};
