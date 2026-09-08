#pragma once

#include "Entity.hpp"
#include "WorldGrid.hpp"

#include <Asset/AssetTicket.hpp>
#include <Object/Object.hpp>
#include <Player.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/RenderList.hpp>


namespace fx {

class Player;
class Blockout;

struct SceneDistanceBand
{
	float32 Distance = 0.0f;
	PagedArray<ObjectID> Objects;
};


class World
{
	struct TransparentObjectCarrier
	{
		ObjectID ID;
		renderer::ePipelineName Pipeline;
		float32 Distance;
	};

public:
	World() = default;

	void Create();

	void Attach(AssetTicket object_ticket);
	void Attach(const Ref<LightBase>& light);

	void Detach(ObjectID id);

	void SelectCamera(const Ref<Camera>& camera) { mpCurrentCamera = camera; }

	void Render(Camera* shadow_camera);

	/**
	 * @brief Renders the 6 cubemap faces for a pending probe capture bake into
	 * the probe capture stage. Called from DoComposition after the main forward
	 * pass has ended. No-op unless ProbeManager::BeginCaptureBake() armed one.
	 */
	void RenderProbeCapture();

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

	FX_FORCE_INLINE Player& GetPlayer() { return this->Player; }

	Ref<PerspectiveCamera>& GetCurrentCamera() { return mpCurrentCamera; }

	~World() { Destroy(); }

private:
	void RenderPhysicsObjects(const Camera& camera);
	void RenderProbeDebug(const Camera& camera);
	void RenderBoundingBoxes(const Camera& camera);
	void RenderWorldGrid(const Camera& camera);

	void ExecuteRenderList(renderer::ePipelineName pl_name);
	void ExecuteRenderList(renderer::ePipelineName pl_name, PerspectiveCamera& camera);
	void ExecuteTransparentRenderLists();
	void ExecuteShadowRenderList(renderer::ePipelineName pl_name);
	void ExecutePrepassRenderList(renderer::ePipelineName pl_name);

	void RebuildRenderList(bool clear, TileIndex new_tile);
	void AddToRenderListRecursive(renderer::ePipelineName pl_name, ObjectID* id);

	void RebuildFromTiles(TileIndex tile_index);

	void SortTransparentObjects(renderer::Pipeline& pipeline, renderer::RenderListSection& section);

public:
	void NotifyObjectMaterialChanged(ObjectID id);

	Name Name = "(unnamed)";
	bool bRenderPhysicsObjects = false;
	bool bRenderProbes = false;
	renderer::RenderList mRenderList;

	/// Set once a scene file has populated its objects. Used by WorldFile to
	/// tell a first load (add everything) from a hot reload (update in place).
	/// NOTE: blockout objects attach independently and must not affect this.
	bool bSceneLoaded = false;

	Player Player;

	Blockout* pBlockout = nullptr;
	String BlockoutPath;

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

	/// Sorted entries, only for transparent objects.
	DynArray<TransparentObjectCarrier> SortedEntryBuffer;
};

} // namespace fx
