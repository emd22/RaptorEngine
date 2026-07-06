#pragma once


#include "Entity.hpp"
#include "TileSystem.hpp"

#include <Asset/AssetTicket.hpp>
#include <Object/Object.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/RenderList.hpp>

namespace fx {

struct SceneDistanceBand
{
	float32 Distance = 0.0f;
	PagedArray<ObjectID> Objects;
};

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
	void RenderPhysicsObjects(const Camera& camera);
	void RenderObjectShadows(Object* object_id);

	void ExecuteRenderList(renderer::ePipelineName pl_name);

	void RebuildRenderList(TileIndex new_tile);

public:
	Name Name = "(unnamed)";
	bool bRenderPhysicsObjects = false;
	renderer::RenderList mRenderList;
	TileSystem mTileSystem;

private:
	PagedArray<ObjectID> mObjects;
	PagedArray<Ref<LightBase>> mLights;
	PagedArray<PhObject> mPhysicsObjects;

	TileIndex mCameraTileIndex = TileIndexNull;

	Ref<PerspectiveCamera> mpCurrentCamera { nullptr };

	PhObjectId mSelectedPhysicsObjectId = PhObjectIdNull;

	Ref<PrimitiveMesh> mpDebugCube { nullptr };
};

} // namespace fx
