#pragma once

#include "ObjectManager.hpp"

#include <Entity.hpp>
#include <Object.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Light.hpp>

namespace fx {

class Scene
{
public:
    Scene() = default;

    void Create();

    void Attach(const TSRef<Object>& object);
    void Attach(const Ref<LightBase>& light);

    void SelectCamera(const Ref<Camera>& camera) { mpCurrentCamera = camera; }

    void Render(Camera* shadow_camera);
    void RenderShadows(Camera* shadow_camera);

    const PagedArray<TSRef<Object>>& GetAllObjects() { return mObjects; }
    const PagedArray<Ref<LightBase>>& GetAllLights() { return mLights; }

    Ref<LightDirectional> GetDirectionalLight()
    {
        for (Ref<LightBase>& light : mLights) {
            if (light->Type == LightType::eDirectional) {
                return Ref<LightDirectional>(light);
            }
        }
        return Ref<LightDirectional>(nullptr);
    }

    TSRef<Object> FindObject(Hash64 name_hash);

    void Destroy();

    ~Scene() { Destroy(); }

private:
    void RenderUnlitObjects(const Camera& camera) const;

public:
    Name Name = "(unnamed)";

private:
    PagedArray<TSRef<Object>> mObjects;
    PagedArray<Ref<LightBase>> mLights;

    Ref<PerspectiveCamera> mpCurrentCamera { nullptr };
};

} // namespace fx
