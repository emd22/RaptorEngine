#pragma once

#include "FxObjectManager.hpp"

#include <FxEntity.hpp>
#include <FxObject.hpp>
#include <Renderer/FxCamera.hpp>
#include <Renderer/FxLight.hpp>

class FxScene
{
public:
    FxScene() = default;

    void Create();

    void Attach(const FxTSRef<FxObject>& object);
    void Attach(const FxRef<FxLightBase>& light);

    void SelectCamera(const FxRef<FxCamera>& camera) { mpCurrentCamera = camera; }

    void Render(FxCamera* shadow_camera);
    void RenderShadows(FxCamera* shadow_camera);

    const FxPagedArray<FxTSRef<FxObject>>& GetAllObjects() { return mObjects; }
    const FxPagedArray<FxRef<FxLightBase>>& GetAllLights() { return mLights; }

    FxRef<FxLightDirectional> GetDirectionalLight()
    {
        for (FxRef<FxLightBase>& light : mLights) {
            if (light->Type == FxLightType::eDirectional) {
                return FxRef<FxLightDirectional>(light);
            }
        }
        return FxRef<FxLightDirectional>(nullptr);
    }

    FxTSRef<FxObject> FindObject(FxHash64 name_hash);

    void Destroy();

    ~FxScene() { Destroy(); }

private:
    void RenderUnlitObjects(const FxCamera& camera) const;

public:
    FxName Name = "(unnamed)";

private:
    FxPagedArray<FxTSRef<FxObject>> mObjects;
    FxPagedArray<FxRef<FxLightBase>> mLights;

    FxRef<FxPerspectiveCamera> mpCurrentCamera { nullptr };
};
