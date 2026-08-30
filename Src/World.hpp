#pragma once

#include "Entity.hpp"
#include "WorldGrid.hpp"

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

class World
{
public:
	World() = default;

	void Create();

	void Attach(AssetTicket object_ticket);
	void Attach(const Ref<LightBase>& light);

	void Detach(ObjectID id);

	void SelectCamera(const Ref<Camera>& camera) { mpCurrentCamera = camera; }

	void Render(Camera* shadow_camera);

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

	void ReleaseAllObjects() { mObjects.Clear(); }

	void Destroy();

	Ref<PerspectiveCamera>& GetCurrentCamera() { return mpCurrentCamera; }

	~World() { Destroy(); }

private:
	void RenderPhysicsObjects(const Camera& camera);
	void RenderBoundingBoxes(const Camera& camera);
	void RenderWorldGrid(const Camera& camera);

	void ExecuteRenderList(renderer::ePipelineName pl_name);
	void ExecuteShadowRenderList(renderer::ePipelineName pl_name);

	void RebuildRenderList(bool clear, TileIndex new_tile);
	void AddToRenderListRecursive(renderer::ePipelineName pl_name, ObjectID* id);

	void RebuildFromTiles(TileIndex tile_index);

public:
	Name Name = "(unnamed)";
	bool bRenderPhysicsObjects = false;
	renderer::RenderList mRenderList;

private:
	PagedArray<ObjectID> mObjects;
	PagedArray<Ref<LightBase>> mLights;

	Ref<PerspectiveCamera> mpCurrentCamera { nullptr };

	physics::BodyID mSelectedPhysicsObjectId = physics::BodyID::scNull;

	Ref<PrimitiveMesh> mpDebugCube { nullptr };


	// Used by RenderPhysicsObjects. Rebuild the physics objects list if there have been changes recorded in the physics
	// manager.
	uint32 mLastPhysicsUpdateState = UINT32_MAX;
	SizedArray<physics::Body*> mCachedPhysicsBodies;
};

} // namespace fx
