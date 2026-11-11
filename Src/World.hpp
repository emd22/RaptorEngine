#pragma once

#include "Entity.hpp"
#include "WorldGrid.hpp"

#include <Asset/AssetTicket.hpp>
#include <Audio/SoundEmitters.hpp>
#include <Map/PortalVisibility.hpp>
#include <Math/Frustum.hpp>
#include <Object/Object.hpp>
#include <Player.hpp>
#include <Renderer/Camera/BaseCamera.hpp>
#include <Renderer/Camera/OrthoCamera.hpp>
#include <Renderer/FogSettings.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/LightList.hpp>
#include <Renderer/ProbeClipmap.hpp>
#include <Renderer/RenderList.hpp>
#include <functional>


namespace fx {

class CVarValue;

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

	struct SpotShadowBake
	{
		LightSpot* pLight = nullptr;

		uint32 FirstCaster = 0;
		uint32 NumCasters = 0;
	};

public:
	World() = default;

	void Create();

	void Attach(AssetTicket object_ticket);

	void Detach(ObjectID id);

	void AttachLoaded(Object* object);

	void SelectCamera(const Ref<FPSCamera>& camera) { mpCurrentCamera = camera; }

	void SetCullCamera(const Ref<FPSCamera>& camera) { mpCullCamera = camera; }

	bool HasDetachedCullCamera() const { return mpCullCamera.IsValid(); }

	void Render(BaseCamera* shadow_camera);

	/**
	 * @brief Captures the next probe of a reflection probe bake. Called from DoComposition after the main forward
	 * pass has ended. No-op unless a bake is running.
	 */
	void RenderProbeCapture();

	void Destroy();

	FX_FORCE_INLINE Player& GetPlayer() { return this->Player; }

	Ref<FPSCamera>& GetCurrentCamera() { return mpCurrentCamera; }

	~World() { Destroy(); }

private:
	void DebugDrawPhysicsBodies();

	/**
	 * @brief Draws a wireframe box for each probe volume brush
	 */
	void DebugDrawProbeVolumes();
	void DebugDrawSelectedDecals();

public:
	Object* RaycastProbeVolumes(const FVec3 origin, const FVec3 direction, float32 max_distance, float32& out_distance,
								const std::function<bool(const Object&)>& accept = nullptr);

private:
	void DebugDrawObjectBounds();
	void DebugDrawLightBounds();
	void DebugDrawWorldGrid();
	void DebugDrawPortals();
	void DebugDrawCullFrustum();

	PerspectiveCamera& GetCullCamera();

	void CullWorldTiles(const PerspectiveCamera& cam);

	void ExecuteRenderList(renderer::PipelineHandle pipeline);

	void ExecuteRenderList(renderer::PipelineHandle pipeline, renderer::PipelineHandle draw_pipeline,
						   PerspectiveCamera& camera);

	void ExecuteForwardRenderLists(PerspectiveCamera& camera);
	void ExecuteTransparentRenderLists();
	void ExecuteShadowRenderList(renderer::PipelineHandle pipeline, const BaseCamera& shadow_camera,
								 bool skip_lightmapped = false);

	bool RenderCaptureSunShadows(LightDirectional& sun, const FVec3 center, OrthoCamera& out_shadow_camera,
								 uint32& out_light_slot);

	/**
	 * @brief Gives each shadowed spot light a tile in the shadow atlas and queues a bake for every light whose tile is
	 * out of date. A bake is out of date when the light, the atlas, or any shadow caster in the light's range (its
	 * transform, or whether it has finished loading) has changed since the tile was last rendered.
	 */
	void UpdateSpotShadows();

	/// Renders the bakes queued by UpdateSpotShadows() into their shadow atlas tiles.
	void BakeSpotShadows();

	/**
	 * @brief Starts a shadow pass over one region of the shadow atlas and binds the shadow pipeline's descriptors.
	 */
	void BeginShadowAtlasRegion(const URect& region);

	/**
	 * @brief Appends the shadow casters (and their attached entities) from every tile that a sphere touches.
	 */
	void GatherSpotShadowCasters(const FVec3 center, float32 radius, DynArray<ObjectID>& out_casters);
	void AddSpotShadowCasterRecursive(ObjectID id, uint32 first_caster, const FVec3 center, float32 radius,
									  DynArray<ObjectID>& out_casters);

	void ExecutePrepassRenderList(renderer::PipelineHandle forward_pipeline);

	void AddLoadedObject(Object* object);

	void AddTileToRenderList(bool clear, TileIndex new_tile, const Frustum* frustum = nullptr,
							 const PortalVisibility* portals = nullptr);
	void AddTileToLightList(TileIndex tile_index, const Frustum* frustum = nullptr,
							const PortalVisibility* portals = nullptr);
	void AddUnculledLightsToLightList();
	/// Adds the culled lights that still reach what the GI probes around `center` can see
	void AddGILightsToLightList(const FVec3& center);
	/// Where the SDF pages and the GI probes are centred this frame
	FVec3 GetGICenter(const BaseCamera& camera) const;
	/// Hands the GI probes the boxes of the objects tagged DynamicGI nearest `center`
	void UpdateDynamicGIProxies(const FVec3& center);
	void ClearRenderList();

	void AddToRenderListRecursive(renderer::PipelineHandle pipeline, ObjectID* id);
	void AddShadowCasterRecursive(ObjectID id);

	/**
	 * @brief Recursively adds `id` and its attached nodes to the geometry render list, deriving each node's own
	 * pipeline from its own material rather than inheriting the pipeline chosen for the root (attached primitives
	 * of a multi-primitive mesh can each require a different pipeline, e.g. a skinned primitive attached to a
	 * container object whose own material differs).
	 */
	void AddToRenderListRecursiveByMaterial(ObjectID* id, const Frustum* frustum, const PortalVisibility* portals);

	void SortTransparentObjects(renderer::Pipeline& pipeline, renderer::RenderListSection& section);

public:
	Name Name = "(unnamed)";
	static constexpr uint32 scDebugBoundsObjects = (1u << 0);
	static constexpr uint32 scDebugBoundsLights = (1u << 1);
	static constexpr uint32 scDebugBoundsPhysics = (1u << 2);
	static constexpr uint32 scDebugBoundsRagdolls = (1u << 3);

	uint32 DebugBoundsMask = 0;

	renderer::RenderList mRenderList;
	renderer::LightList mLightList;


	/// Set once a scene file has populated its objects. Used by WorldFile to
	/// tell a first load (add everything) from a hot reload (update in place)
	bool bIsPopulated = false;

	Player Player;

	/// Where the player starts a level, and which way it looks. `bCustom` is false until the level sets one.
	struct PlayerSpawnPoint
	{
		static FVec3 DefaultPosition() { return FVec3(0.0f, -0.2f, -2.0f); }

		FVec3 Position = DefaultPosition();
		FVec3 Direction = FVec3::sForward;
		bool bCustom = false;

		void Reset()
		{
			Position = DefaultPosition();
			Direction = FVec3::sForward;
			bCustom = false;
		}
	} PlayerSpawn;

	void RespawnPlayer() { Player.SpawnAt(PlayerSpawn.Position, PlayerSpawn.Direction); }

	renderer::FogSettings Fog;

	SoundEmitterSet SoundEmitters;

	Blockout* pPrototype = nullptr;
	String PrototypePath;

	CVarValue* pCVarShowProbeVolumes = nullptr;
	CVarValue* pCVarFrustumCull = nullptr;
	CVarValue* pCVarPortalCull = nullptr;
	CVarValue* pCVarPortalLock = nullptr;
	CVarValue* pCVarPortalDebug = nullptr;
	CVarValue* pCVarBakedShadows = nullptr;
	CVarValue* pCVarSDFPageDebug = nullptr;
	CVarValue* pCVarSDFPageBudget = nullptr;
	CVarValue* pCVarSDFPageCone = nullptr;
	CVarValue* pCVarSDFPageRayLength = nullptr;
	CVarValue* pCVarSDFPageProbeStride = nullptr;
	CVarValue* pCVarGIClipmapDebug = nullptr;
	CVarValue* pCVarGIProbeBudget = nullptr;
	CVarValue* pCVarGIHysteresis = nullptr;
	CVarValue* pCVarGIBounce = nullptr;
	CVarValue* pCVarGIRelocate = nullptr;
	CVarValue* pCVarGIRayLength = nullptr;
	CVarValue* pCVarGIDirty = nullptr;
	CVarValue* pCVarGIProbeMask = nullptr;
	CVarValue* pCVarGIDynamic = nullptr;

	uint32 FrustumTestedObjects = 0;
	uint32 FrustumCulledObjects = 0;
	uint32 PortalCulledObjects = 0;
	uint32 PortalCulledLights = 0;

	const PortalVisibility& GetPortalVisibility() const { return mPortalVisibility; }

private:
	Ref<FPSCamera> mpCurrentCamera { nullptr };
	Ref<FPSCamera> mpCullCamera { nullptr };

	physics::BodyID mSelectedPhysicsObjectId = physics::BodyID::scNull;

	uint32 mLastPhysicsUpdateState = UINT32_MAX;
	SizedArray<physics::Body*> mCachedPhysicsBodies;

	DynArray<TransparentObjectCarrier> SortedEntryBuffer;

	uint32 mSunLightSlot = UINT32_MAX;

	/// Persistent lights are written after this many light buffer slots, so probe captures can leave them out
	uint32 mMapLightSlotCount = 0;

	/// Spot light shadow maps to render this frame, see UpdateSpotShadows()
	DynArray<SpotShadowBake> mSpotShadowBakes;
	DynArray<ObjectID> mSpotShadowCasters;

	Frustum mFrustum;
	PortalVisibility mPortalVisibility;

	bool mbPortalLocked = false;
	FMat4 mLockedViewProjection;
	FVec3 mLockedCameraPosition = FVec3::sZero;
	FVec3 mLockedCameraDirection = FVec3::sForward;

	SizedArray<TileIndex> mVisibleTiles;
	uint64 mCullSerial = 0;

	/// Every box of the objects tagged DynamicGI this frame, before the nearest are kept
	SizedArray<renderer::DynamicGIProxy> mDynamicGIProxies;
};

} // namespace fx
