#pragma once

#include "Object/ObjectManager.hpp"

#include <Asset/AssetTicket.hpp>
#include <Entity.hpp>
#include <Object/Object.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/RenderList.hpp>

namespace fx {

class Scene
{
public:
    Scene() = default;

    void Create();

    void Attach(AssetTicket<Object> object_ticket);
    void Attach(Object* object);

    void Attach(const Ref<LightBase>& light);

    PhObjectId NewPhysicsObject();
    PhObject* GetPhysicsObject(PhObjectId id) const;

    void SelectCamera(const Ref<Camera>& camera) { mpCurrentCamera = camera; }

    void Render(Camera* shadow_camera);
    void RenderShadows(Camera* shadow_camera);

    const PagedArray<ObjectID>& GetAllObjects() { return mObjects; }
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

    Object* FindObject(const Hash32 name_hash);
    PhObject* FindPhysicsObject(const Hash32 name_hash);

    void SelectPhysicsObject(const JPH::BodyID& body_id);
    PhObjectId GetSelectedPhysicsObject() const { return mSelectedPhysicsObjectId; }

    void ReleaseAllObjects() { mObjects.Clear(); }

    void Destroy();

    Ref<PerspectiveCamera>& GetCurrentCamera() { return mpCurrentCamera; }

    ~Scene() { Destroy(); }

private:
    void RenderUnlitObjects(const Camera& camera) const;
    void RenderPhysicsObjects(const Camera& camera);

    void RenderObjectShadows(Object* object_id);

    void RenderRLSection(const renderer::RenderListSection& section);

public:
    Name Name = "(unnamed)";

    bool bRenderPhysicsObjects = false;

    renderer::RenderList mRenderList;

private:
    PagedArray<ObjectID> mObjects;
    PagedArray<Ref<LightBase>> mLights;
    PagedArray<PhObject> mPhysicsObjects;

    Ref<PerspectiveCamera> mpCurrentCamera { nullptr };

    PhObjectId mSelectedPhysicsObjectId = PhObjectIdNull;

    Ref<PrimitiveMesh> mpDebugCube { nullptr };
};

} // namespace fx
