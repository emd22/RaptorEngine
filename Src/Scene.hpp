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

    PhObjectId NewPhysicsObject();
    PhObject* GetPhysicsObject(PhObjectId id);

    void SelectCamera(const Ref<Camera>& camera) { mpCurrentCamera = camera; }

    void Render(Camera* shadow_camera);
    void RenderShadows(Camera* shadow_camera);


    const PagedArray<TSRef<Object>>& GetAllObjects() { return mObjects; }
    const PagedArray<Ref<LightBase>>& GetAllLights() { return mLights; }

    Ref<LightDirectional> GetDirectionalLight()
    {
        for (Ref<LightBase>& light : mLights) {
            if (light->Type == eLightType::Directional) {
                return Ref<LightDirectional>(light);
            }
        }
        return Ref<LightDirectional>(nullptr);
    }

    TSRef<Object> FindObject(Hash32 name_hash);

    void Destroy();

    ~Scene() { Destroy(); }

private:
    void RenderUnlitObjects(const Camera& camera) const;
    void RenderPhysicsObjects(const Camera& camera);

public:
    Name Name = "(unnamed)";

    bool bRenderPhysicsObjects = true;

private:
    PagedArray<TSRef<Object>> mObjects;
    PagedArray<Ref<LightBase>> mLights;
    PagedArray<PhObject> mPhysicsObjects;

    Ref<PerspectiveCamera> mpCurrentCamera { nullptr };


    Ref<PrimitiveMesh> mpDebugCube { nullptr };
};

} // namespace fx
