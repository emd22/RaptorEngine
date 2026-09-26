#pragma once

#include "Entity.hpp"
#include "WorldGrid.hpp"

#include <Asset/AssetTicket.hpp>
#include <Math/Frustum.hpp>
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
		renderer::PipelineHandle Pipeline;
		float32 Distance;
	};

	/// A spot light whose shadow map is rendered into the shadow atlas this frame
	struct SpotShadowBake
	{
		LightSpot* pLight = nullptr;

		/// Range of the light's casters in mSpotShadowCasters
		uint32 FirstCaster = 0;
		uint32 NumCasters = 0;
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
	 * @brief Captures the next batch of probes for a light probe bake. Called from DoComposition after the main forward
	 * pass has ended. No-op unless a bake is running.
	 */
	void RenderProbeCapture();

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

	/// Finds an attached light by its name, returns a null ref if there is no match.
	Ref<LightBase> FindLight(Hash32 name)
	{
		for (Ref<LightBase>& light : mLights) {
			if (light->Name == name) {
				return light;
			}
		}

		return Ref<LightBase>(nullptr);
	}


	void Destroy();

	FX_FORCE_INLINE Player& GetPlayer() { return this->Player; }

	Ref<PerspectiveCamera>& GetCurrentCamera() { return mpCurrentCamera; }

	~World() { Destroy(); }

private:
	void RenderPhysicsObjects(const Camera& camera);
	void RenderProbeDebug(const Camera& camera);

	/// Draws a wireframe box for each probe volume brush, which is otherwise not drawn at all
	void RenderProbeVolumes(const Camera& camera);

public:
	Object* RaycastProbeVolumes(const Vec3f& origin, const Vec3f& direction, float32 max_distance,
								float32& out_distance);

private:
	void RenderBoundingBoxes(const Camera& camera);
	void RenderWorldGrid(const Camera& camera);

	void CullWorldTiles(const PerspectiveCamera& cam);

	void ExecuteRenderList(renderer::PipelineHandle pipeline);
	void ExecuteRenderList(renderer::PipelineHandle pipeline, PerspectiveCamera& camera);

	/// Draws the opaque geometry with every pipeline of the forward pass
	void ExecuteForwardRenderLists(PerspectiveCamera& camera);
	void ExecuteTransparentRenderLists();
	void ExecuteShadowRenderList(renderer::PipelineHandle pipeline, const Camera& shadow_camera);

	/**
	 * @brief Re-renders the sun's shadow map centered on `center` for a probe capture, and writes a copy of the sun
	 * that reads it into a spare light buffer slot (the main view keeps the original slot and its shadow matrix).
	 * @return False if the light buffer is full, in which case the shadow map is left alone.
	 */
	bool RenderCaptureSunShadows(LightDirectional& sun, const Vec3f& center, OrthoCamera& out_shadow_camera,
								 uint32& out_light_slot);

	/**
	 * @brief Gives each shadowed spot light a tile in the shadow atlas and queues a bake for every light whose tile is
	 * out of date. A bake is out of date when the light, the atlas, or any shadow caster in the light's range (its
	 * transform, or whether it has finished loading) has changed since the tile was last rendered.
	 */
	void UpdateSpotShadows();

	/// Renders the bakes queued by UpdateSpotShadows() into their shadow atlas tiles.
	void BakeSpotShadows();

	/// Starts a shadow pass over one region of the shadow atlas and binds the shadow pipeline's descriptors.
	void BeginShadowAtlasRegion(const renderer::ShadowAtlasRegion& region);

	/**
	 * @brief Appends the shadow casters (and their attached nodes) from every tile that a sphere touches. Skinned
	 * objects are skipped, the shadow pipeline cannot draw them.
	 */
	void GatherSpotShadowCasters(const Vec3f& center, float32 radius, DynArray<ObjectID>& out_casters);
	void AddSpotShadowCasterRecursive(ObjectID id, uint32 first_caster, DynArray<ObjectID>& out_casters);
	/// Draws the objects of a forward pipeline's list into the prepass, with the prepass pipeline that goes with it
	void ExecutePrepassRenderList(renderer::PipelineHandle forward_pipeline);

	void AddTileToRenderList(bool clear, TileIndex new_tile);
	void ClearRenderList();

	void AddToRenderListRecursive(renderer::PipelineHandle pipeline, ObjectID* id);
	/**
	 * @brief Recursively adds `id` and its attached nodes to the geometry render list, deriving each node's own
	 * pipeline from its own material rather than inheriting the pipeline chosen for the root (attached primitives
	 * of a multi-primitive mesh can each require a different pipeline, e.g. a skinned primitive attached to a
	 * container object whose own material differs).
	 */
	void AddToRenderListRecursiveByMaterial(ObjectID* id);

	void SortTransparentObjects(renderer::Pipeline& pipeline, renderer::RenderListSection& section);

public:
	Name Name = "(unnamed)";
	bool bRenderPhysicsObjects = false;
	bool bRenderProbes = false;
	renderer::RenderList mRenderList;

	/// Set once a scene file has populated its objects. Used by WorldFile to
	/// tell a first load (add everything) from a hot reload (update in place)
	bool bIsPopulated = false;

	Player Player;

	Blockout* pBlockout = nullptr;
	String BlockoutPath;

private:
	PagedArray<Ref<LightBase>> mLights;

	Ref<PerspectiveCamera> mpCurrentCamera { nullptr };

	physics::BodyID mSelectedPhysicsObjectId = physics::BodyID::scNull;

	Ref<PrimitiveMesh> mpWireBox { nullptr };
	Ref<PrimitiveMesh> mpDebugCube { nullptr };

	uint32 mLastPhysicsUpdateState = UINT32_MAX;
	SizedArray<physics::Body*> mCachedPhysicsBodies;

	DynArray<TransparentObjectCarrier> SortedEntryBuffer;

	uint32 mSunLightSlot = UINT32_MAX;

	/// Spot light shadow maps to render this frame, see UpdateSpotShadows()
	DynArray<SpotShadowBake> mSpotShadowBakes;
	DynArray<ObjectID> mSpotShadowCasters;

	Frustum mFrustum;

	SizedArray<TileIndex> mVisibleTiles;
};

} // namespace fx
