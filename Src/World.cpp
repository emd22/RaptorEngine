#include "World.hpp"

#include <Blockout.hpp>
#include <CVar.hpp>
#include <Decal/DecalManager.hpp>
#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Material/MaterialManagerFwd.hpp>
#include <Math/RayCast.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/LightManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/ShadowAtlas.hpp>
#include <Renderer/ShadowDirectional.hpp>
#include <algorithm>

namespace fx {


using namespace renderer;

void World::Create()
{
	SortedEntryBuffer.SetPageSize(128);

	mSpotShadowBakes.SetPageSize(ShadowAtlas::scMaxSpotTiles);
	mSpotShadowCasters.SetPageSize(256);

	mVisibleTiles.InitCapacity(600);

	pCVarShowProbeVolumes = gCVars->Set<int64>("r_show_volumes", 0);
	pCVarFrustumCull = gCVars->Set<int64>("r_frustum_cull", 1);
	pCVarPortalCull = gCVars->Set<int64>("r_portal_cull", 1);
	pCVarPortalLock = gCVars->Set<int64>("r_portal_lock", 0);
	pCVarPortalDebug = gCVars->Set<int64>("r_portal_debug", 0);
	pCVarBakedShadows = gCVars->Set<int64>("r_baked_shadows", 1);
	pCVarBakedIndirect = gCVars->Set<int64>("r_baked_indirect", 1);
}

static bool IsTransparentMaterial(Material* mat) { return (mat != nullptr && mat->IsTransparent()); }

static bool CastsShadows(const Object& object)
{
	if (!object.IsShadowCaster()) {
		return false;
	}

	const Material* material = MaterialManagerFwd::GetMaterial(object.GetMaterialID());

	return (material == nullptr || !material->IsGlass());
}

/**
 * @brief The geometry pipeline that draws a material, including the transparent variants.
 */
static PipelineHandle GetPipelineForObject(const Object& object)
{
	Material* material = MaterialManagerFwd::GetMaterial(object.GetMaterialID());

	if (material == nullptr) {
		material = MaterialManagerFwd::GetMaterial(MaterialID::scNull);
	}

	ePipelinePass pass = ePipelinePass::Forward;

	if (IsTransparentMaterial(material) || object.IsDecal()) {
		pass = ePipelinePass::ForwardBlend;
	}

	ePipelineFeatures features = material->GetPipelineFeatures();

	if (object.IsLightmapped()) {
		features |= ePipelineFeatures::Lightmap;
	}

	if (object.IsVertexColored() && object.pMesh.IsValid() && object.pMesh->HasVertexColors()) {
		features |= ePipelineFeatures::VertexColor;
	}

	const PipelineHandle pipeline = gPipelineCache->GetOrCreateVariant(pass, features);

	AssertMsg(pipeline.IsValid(), "No forward pipeline draws this material");

	return pipeline;
}

/**
 * @brief The render list section that objects that cast shadows are added to
 */
static PipelineHandle GetShadowListPipeline()
{
	return gPipelineCache->GetOrCreateVariant(ePipelinePass::Shadow, ePipelineFeatures::None);
}

static void AddObjectToRenderList(Object* object, World* scene)
{
	if (object->pMesh.IsValid()) {
		// If the object casts shadows as well, add it to the shadow section
		if (CastsShadows(*object)) {
			gWorld->mRenderList.AddObject(GetShadowListPipeline(), object->ID);
		}

		if (!object->IsShadowOnly()) {
			gWorld->mRenderList.AddObject(GetPipelineForObject(*object), object->ID);
		}
	}

	if (!object->AttachedNodes.IsEmpty()) {
		for (const ObjectID& attach_id : object->AttachedNodes) {
			Object* attached_object = gObjectManager->GetObject(attach_id);

			if (attached_object == nullptr) {
				continue;
			}

			if (object->IsShadowCaster()) {
				attached_object->SetShadowCaster(true);
			}

			AddObjectToRenderList(attached_object, scene);
		}
	}
}

// static void RemoveObjectFromRenderList(ObjectID id)
// {
// 	if (id.IsNull() || id.IsInvalid()) {
// 		return;
// 	}


// 	Object* object = gObjectManager->GetObject(id);
// 	gWorld->mRenderList.RemoveAllOfObject(id);

// 	if (!object->AttachedNodes.IsEmpty()) {
// 		for (const ObjectID& attach_id : object->AttachedNodes) {
// 			RemoveObjectFromRenderList(attach_id);
// 		}
// 	}
// }


void World::Attach(AssetTicket object_ticket)
{
	Object* object = static_cast<Object*>(object_ticket.Get());

	object->OnAttached(this);

	const ObjectID object_id = object->ID;

	object_ticket.OnLoaded(
		[this, object_id](void* item_ptr)
		{
			Object* loaded_object = static_cast<Object*>(item_ptr);

			if (gObjectManager->GetObject(object_id) != loaded_object) {
				return;
			}

			AddLoadedObject(loaded_object);
		});
}

void World::Detach(ObjectID id)
{
	mRenderList.InvalidateObject(id);
	gWorldGrid->RemoveObject(id);
}

void World::AttachLoaded(Object* object)
{
	object->OnAttached(this);
	AddLoadedObject(object);
}

void World::AddLoadedObject(Object* object)
{
	object->bIsAddedToWorld.store(true);
	AddObjectToRenderList(object, this);
	gWorldGrid->AddObject(object->ID);
}


void World::ExecuteRenderList(renderer::PipelineHandle pipeline_handle)
{
	PerspectiveCamera& camera = *mpCurrentCamera;
	ExecuteRenderList(pipeline_handle, pipeline_handle, camera);
}

void World::ExecuteForwardRenderLists(PerspectiveCamera& camera)
{
	// The forward pipelines depth test against the prepass, which probe captures don't have
	const bool is_probe_capture = gProbeManager->IsCapturingFaces();
	const bool is_debug_view = gGraphics->HasDebugView();

	gPipelineCache->ForEachPassPipeline(
		ePipelinePass::Forward,
		[&](PipelineHandle pipeline)
		{
			PipelineHandle draw_pipeline = pipeline;

			if (is_probe_capture) {
				draw_pipeline = gPipelineCache->GetOrCreateVariantInPass(pipeline, ePipelinePass::ForwardCapture);
			}
			else if (is_debug_view) {
				draw_pipeline = gPipelineCache->GetOrCreateVariantInPass(pipeline, ePipelinePass::ForwardDebug);
			}

			ExecuteRenderList(pipeline, draw_pipeline, camera);
		});
}

void World::ExecuteRenderList(renderer::PipelineHandle pipeline_handle, renderer::PipelineHandle draw_pipeline_handle,
							  PerspectiveCamera& camera)
{
	RenderListSection& section = mRenderList.GetSection(pipeline_handle);

	if (!draw_pipeline_handle.IsValid()) {
		return;
	}

	renderer::Pipeline& pipeline = gPipelineCache->Get(draw_pipeline_handle);

	if (!pipeline.IsBuilt()) {
		return;
	}

	pipeline.Bind(gGraphics->GetFrame()->CmdBuffer);

	{
		const uint32 buffer_offsets[] = {
			gObjectManager->GetBaseOffset(),
			0,
			gGraphics->GetLightGridFrameOffset(),
			gGraphics->GetLightIndexListFrameOffset(),
			// The light probe buffers aren't paged per frame in flight
			0,
			0,
			gGraphics->GetDecalFrameOffset(),
			gGraphics->GetDecalMaskFrameOffset(),
			0,
			0,
		};

		gGraphics->pRenderer->pPersistentDescriptor->Bind(
			0, gGraphics->GetFrame()->CmdBuffer, pipeline,
			Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
	}


	// Light probes only capture the level, not the view model or anything else marked as not probe visible
	const bool is_probe_capture = gProbeManager->IsCapturingFaces();

	for (ObjectID object_id : section.Objects) {
		Object* object = gObjectManager->GetObject(object_id);
		if (object == nullptr || (is_probe_capture && !object->IsProbeVisible())) {
			continue;
		}

		object->Update();

		object->RenderShallow(camera, &pipeline);
	}
}

void World::ExecuteTransparentRenderLists()
{
	PerspectiveCamera& camera = *mpCurrentCamera;
	const Vec3f camera_position = camera.GetPosition();
	;

	auto Gather = [&](PipelineHandle pipeline)
	{
		const RenderListSection& section = mRenderList.GetSection(pipeline);

		for (ObjectID object_id : section.Objects) {
			if (object_id.IsInvalid()) {
				continue;
			}
			Object* object = gObjectManager->GetObject(object_id);
			if (object == nullptr) {
				continue;
			}

			Vec3f center = object->GetPosition() + object->Bounds.Min + object->Bounds.GetSize() * 0.5f;
			Vec3f diff = center - camera_position;

			float32 distance_sq = diff.Dot(diff);

			SortedEntryBuffer.Insert({ object_id, pipeline, distance_sq });
		}
	};

	SortedEntryBuffer.Clear();

	// We store everything in one buffer as the order matters here. The furthest objects should be rendered first.

	gPipelineCache->ForEachPassPipeline(ePipelinePass::ForwardBlend, Gather);

	if (SortedEntryBuffer.Size == 0) {
		return;
	}

	std::sort(SortedEntryBuffer.pData, SortedEntryBuffer.pData + SortedEntryBuffer.Size,
			  [](const TransparentObjectCarrier& a, const TransparentObjectCarrier& b)
			  { return a.Distance > b.Distance; });

	const bool is_debug_view = gGraphics->HasDebugView();

	// Bind per entry as pipeline changes; minimize binds by caching last pipeline
	renderer::Pipeline* pipeline = nullptr;
	PipelineHandle pipeline_handle;
	bool first = true;

	for (const TransparentObjectCarrier& transparent_object : SortedEntryBuffer) {
		// For each differing pipeline (per object), bind the pipeline.
		if (first || !(transparent_object.Pipeline == pipeline_handle)) {
			const PipelineHandle draw_pipeline = is_debug_view ? gPipelineCache->GetOrCreateVariantInPass(
																	 transparent_object.Pipeline,
																	 ePipelinePass::ForwardBlendDebug)
															   : transparent_object.Pipeline;

			if (!draw_pipeline.IsValid()) {
				continue;
			}

			pipeline = &gPipelineCache->Get(draw_pipeline);

			if (!pipeline->IsBuilt()) {
				continue;
			}
			pipeline->Bind(gGraphics->GetFrame()->CmdBuffer);

			{
				const uint32 buffer_offsets[] = {
					gObjectManager->GetBaseOffset(),
					0,
					gGraphics->GetLightGridFrameOffset(),
					gGraphics->GetLightIndexListFrameOffset(),
					// The light probe buffers aren't paged per frame in flight
					0,
					0,
					gGraphics->GetDecalFrameOffset(),
					gGraphics->GetDecalMaskFrameOffset(),
					0,
					0,
				};

				gGraphics->pRenderer->pPersistentDescriptor->Bind(
					0, gGraphics->GetFrame()->CmdBuffer, *pipeline,
					Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
			}

			pipeline_handle = transparent_object.Pipeline;
			first = false;
		}

		Object* object = gObjectManager->GetObject(transparent_object.ID);

		object->Update();

		object->RenderShallow(camera, pipeline);
	}
}


/**
 * @brief Picks between the opaque and the alpha masked shadow pipeline as casters are drawn into a shadow atlas region,
 * so only alpha masked casters pay for sampling their albedo.
 */
class ShadowPipelineSelector
{
public:
	/// Binds whichever pipeline `object` needs (and its albedo, if masked), returning it for the push constants.
	Pipeline& Select(Object* object, const CommandBuffer& cmd)
	{
		const bool masked = IsMasked(object);
		const bool skinned = object->IsSkinned();

		const PipelineHandle wanted = gPipelineCache->GetOrCreateVariant(
			ePipelinePass::Shadow,
			skinned ? ePipelineFeatures::Skinned : (masked ? ePipelineFeatures::AlphaMask : ePipelineFeatures::None));

		if (wanted != mBound) {
			gShadowAtlas->BindPipeline(wanted);
			mBound = wanted;

			const uint32 buffer_offsets[] = { gObjectManager->GetBaseOffset(), 0 };

			gGraphics->pRenderer->pPersistentDescriptorSlim->Bind(
				0, cmd, gPipelineCache->Get(wanted), Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
		}

		Pipeline& pipeline = gPipelineCache->Get(mBound);

		if (masked || skinned) {
			if (!gMaterialManager->BindWithPipeline(cmd, pipeline, object->GetMaterialID())) {
				gMaterialManager->BindWithPipeline(cmd, pipeline, MaterialID::scNull);
			}
		}

		return pipeline;
	}

private:
	static bool IsMasked(Object* object)
	{
		if (object->IsSkinned()) {
			return false;
		}

		Material* material = gMaterialManager->GetMaterial(object->GetMaterialID());

		return material != nullptr && HasFlag(material->Properties.Flags, eMaterialFlags::AlphaMask) &&
			   material->IsReady();
	}

private:
	/// Every region starts out with the opaque pipeline bound
	PipelineHandle mBound = gPipelineCache->GetOrCreateVariant(ePipelinePass::Shadow, ePipelineFeatures::None);
};

void World::ExecuteShadowRenderList(renderer::PipelineHandle pipeline, const BaseCamera& shadow_camera,
									bool skip_lightmapped)
{
	const RenderListSection& section = mRenderList.GetSection(pipeline);

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	ShadowPipelineSelector selector;

	ShadowViewConstants view_consts;
	memcpy(view_consts.ViewProjection, shadow_camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData,
		   sizeof(float32) * 16);

	const uint32 view_offset = gGraphics->FrameAlloc.Push(view_consts);

	ShadowPushConstants consts;

	// The sun shadows rendered for a probe capture only come from casters the probes can see, so objects left out of
	// the capture don't shadow it either
	const bool is_probe_capture = gProbeManager->IsCapturingFaces();

	Frustum shadow_frustum;
	shadow_frustum.Rebuild(shadow_camera, eObjectLayer::WorldLayer);

	const bool cull_to_shadow_frustum = (pCVarFrustumCull->IntValue != 0);

	for (ObjectID object_id : section.Objects) {
		if (object_id.IsInvalid()) {
			continue;
		}

		Object* object = gObjectManager->GetObject(object_id);
		if (object == nullptr ||
			(is_probe_capture && (!object->IsProbeVisible() || object->HasTransparentMaterial()))) {
			continue;
		}

		if (skip_lightmapped && object->IsLightmapped()) {
			continue;
		}

		if (cull_to_shadow_frustum && object->IsOutsideFrustum(shadow_frustum, scFrustumSidePlanes)) {
			continue;
		}

		object->Update();
		object->UpdateIfOutOfDate();

		Pipeline& shadow_pipeline = selector.Select(object, cmd);
		gGraphics->FrameAlloc.Bind(cmd, shadow_pipeline, view_offset);

		// Push the direct index for the object id
		consts.ObjectIndex = object->GetDrawIndex();
		consts.BoneBase = object->BoneBufferBase;
		consts.MaterialIndex = object->GetMaterialID().GetID();
		gGraphics->SubmitPushConstants(cmd, shadow_pipeline, eShaderType::Vertex, consts);
		object->RenderPrimitive(cmd);
	}
}


void World::UpdateSpotShadows()
{
	mSpotShadowBakes.Clear();
	mSpotShadowCasters.Clear();

	for (const LightID light_id : mLightList.GetLights()) {
		LightBase* light_base = gLightManager->GetLight(light_id);

		if (light_base == nullptr || light_base->Type != eLightType::Spot) {
			continue;
		}

		LightSpot* light = static_cast<LightSpot*>(light_base);
		LightSpot::ShadowState& shadow = light->Shadow;

		if (!light->bCastShadows) {
			light->ReleaseShadowTile();
			continue;
		}

		// Disabled lights hold on to their tile, so turning them back on doesn't need a new bake
		if (!light->bEnabled) {
			continue;
		}

		if (shadow.AtlasTile == ShadowTileIndexNull) {
			shadow.AtlasTile = gShadowAtlas->AllocateSpotTile();

			if (shadow.AtlasTile == ShadowTileIndexNull) {
				static bool sbWarned = false;
				if (!sbWarned) {
					LogWarning(LC_RENDER, "Shadow atlas is out of spot light tiles ({}), '{}' will not cast shadows",
							   ShadowAtlas::scUsableSpotTiles, light->Name.Get());
					sbWarned = true;
				}

				continue;
			}
		}

		const Mat4f shadow_matrix = light->CalculateShadowMatrix();

		const uint32 first_caster = mSpotShadowCasters.Size;
		GatherSpotShadowCasters(light->GetPosition(), light->GetRadius(), mSpotShadowCasters);

		const uint64 grid_serial = gWorldGrid->GetChangeSerial();

		// The tile already holds this exact bake
		if (shadow.bBaked && shadow.BakedGeneration == gShadowAtlas->GetGeneration() &&
			memcmp(shadow.Matrix.RawData, shadow_matrix.RawData, sizeof(shadow_matrix.RawData)) == 0 &&
			!gWorldGrid->HasChangedSince(light->GetPosition(), light->GetRadius(), shadow.BakedSerial)) {
			while (mSpotShadowCasters.Size > first_caster) {
				mSpotShadowCasters.RemoveLast();
			}

			continue;
		}

		shadow.Matrix = shadow_matrix;
		shadow.BakedSerial = grid_serial;
		shadow.BakedGeneration = gShadowAtlas->GetGeneration();
		shadow.bBaked = true;

		mSpotShadowBakes.Insert(SpotShadowBake {
			.pLight = light,
			.FirstCaster = first_caster,
			.NumCasters = mSpotShadowCasters.Size - first_caster,
		});
	}
}


void World::BeginShadowAtlasRegion(const URect& region)
{
	gShadowAtlas->BeginRegion(region);

	Pipeline& pipeline = gPipelineCache->Get(GetShadowListPipeline());

	const uint32 buffer_offsets[] = { gObjectManager->GetBaseOffset(), 0 };

	gGraphics->pRenderer->pPersistentDescriptorSlim->Bind(
		0, gGraphics->GetFrame()->CmdBuffer, pipeline, Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
}


void World::BakeSpotShadows()
{
	if (mSpotShadowBakes.Size == 0) {
		return;
	}

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	ShadowPushConstants consts;

	for (const SpotShadowBake& bake : mSpotShadowBakes) {
		const LightSpot::ShadowState& shadow = bake.pLight->Shadow;

		BeginShadowAtlasRegion(gShadowAtlas->GetSpotTileRegion(shadow.AtlasTile));

		ShadowPipelineSelector selector;

		ShadowViewConstants view_consts;
		memcpy(view_consts.ViewProjection, shadow.Matrix.RawData, sizeof(float32) * 16);

		const uint32 view_offset = gGraphics->FrameAlloc.Push(view_consts);

		const uint32 end_caster = bake.FirstCaster + bake.NumCasters;

		for (uint32 index = bake.FirstCaster; index < end_caster; index++) {
			const ObjectID caster_id = mSpotShadowCasters[index];

			Object* caster = gObjectManager->GetObject(caster_id);
			if (caster == nullptr) {
				continue;
			}

			// Casters out of view skip the forward pass, which is what normally uploads their matrix for this frame
			gObjectManager->Submit(caster_id, caster->GetWorldMatrix());

			Pipeline& shadow_pipeline = selector.Select(caster, cmd);
			gGraphics->FrameAlloc.Bind(cmd, shadow_pipeline, view_offset);

			consts.ObjectIndex = caster->GetDrawIndex();
			consts.BoneBase = caster->BoneBufferBase;
			consts.MaterialIndex = caster->GetMaterialID().GetID();
			gGraphics->SubmitPushConstants(cmd, shadow_pipeline, eShaderType::Vertex, consts);
			caster->RenderPrimitive(cmd);
		}

		gShadowAtlas->EndRegion();
	}
}


void World::GatherSpotShadowCasters(const Vec3f center, float32 radius, DynArray<ObjectID>& out_casters)
{
	const uint32 first_caster = out_casters.Size;

	auto add_casters_from_tile = [&](TileIndex tile_index)
	{
		const Tile* tile = gWorldGrid->GetTile(tile_index);
		if (tile == nullptr) {
			return;
		}

		uint32 index = 0;
		while (true) {
			index = tile->Objects.SlotsInUse.FindNextSetBit(index);
			if (index == Bitset::scNoFreeBits) {
				break;
			}

			const ObjectID* object_id = tile->Objects.GetItem(index);
			++index;

			if (object_id == nullptr) {
				continue;
			}

			Object* object = gObjectManager->GetObject(*object_id);

			// Attached nodes cast shadows along with their root, same as the directional light's render list
			if (object != nullptr && object->IsShadowCaster()) {
				AddSpotShadowCasterRecursive(*object_id, first_caster, center, radius, out_casters);
			}
		}
	};

	Vec2u min_tile;
	Vec2u max_tile;
	gWorldGrid->GetSphereTileRange(center, radius, &min_tile, &max_tile);

	for (uint32 y = min_tile.Y; y <= max_tile.Y; y++) {
		for (uint32 x = min_tile.X; x <= max_tile.X; x++) {
			add_casters_from_tile(gWorldGrid->TileFromTileXY(Vec2u(x, y)));
		}
	}

	add_casters_from_tile(WorldGrid::scGlobalTileIndex);
}


static bool SkinnedCasterReachesSphere(Object& object, const Vec3f center, float32 radius)
{
	constexpr float32 scMeshPadding = 0.3f;

	if (!object.pSkeleton.IsValid()) {
		return false;
	}

	object.UpdateAnimation();

	if (!object.HasBonesForDraw()) {
		return false;
	}

	const Skeleton& skeleton = *object.pSkeleton;
	const float32* m = object.GetWorldMatrix().RawData;
	const Vec3f c = skeleton.PoseCenter;

	const Vec3f pose_center(c.X * m[0] + c.Y * m[4] + c.Z * m[8] + m[12], c.X * m[1] + c.Y * m[5] + c.Z * m[9] + m[13],
							c.X * m[2] + c.Y * m[6] + c.Z * m[10] + m[14]);

	const float32 reach = (skeleton.PoseRadius + scMeshPadding) * object.mScale + radius;

	return (pose_center - center).Length() <= reach;
}

void World::AddSpotShadowCasterRecursive(ObjectID id, uint32 first_caster, const Vec3f center, float32 radius,
										 DynArray<ObjectID>& out_casters)
{
	if (id.IsInvalid()) {
		return;
	}

	Object* object = gObjectManager->GetObject(id);
	if (object == nullptr) {
		return;
	}

	// Objects that span several tiles are found more than once
	for (uint32 index = first_caster; index < out_casters.Size; index++) {
		if (out_casters[index] == id) {
			return;
		}
	}

	// Polls for a mesh that finished loading, which marks the object's tiles as changed
	object->CheckIfReady(false);

	if (CastsShadows(*object) && (!object->IsSkinned() || SkinnedCasterReachesSphere(*object, center, radius))) {
		out_casters.Insert(id);
	}

	for (ObjectID& attached_id : object->AttachedNodes) {
		AddSpotShadowCasterRecursive(attached_id, first_caster, center, radius, out_casters);
	}
}

void World::ExecutePrepassRenderList(renderer::PipelineHandle forward_pipeline)
{
	// Only the opaque pipelines have a prepass one. The blended ones don't write depth, they are blended in forward.
	const PipelineHandle prepass_pipeline = gPipelineCache->GetOrCreateVariantInPass(forward_pipeline,
																					 ePipelinePass::Depth);

	if (!prepass_pipeline.IsValid()) {
		return;
	}

	PerspectiveCamera& camera = *mpCurrentCamera;

	const RenderListSection& section = mRenderList.GetSection(forward_pipeline);

	renderer::Pipeline& pipeline = gPipelineCache->Get(prepass_pipeline);

	if (!pipeline.IsBuilt()) {
		return;
	}

	pipeline.Bind(gGraphics->GetFrame()->CmdBuffer);

	{
		const uint32 buffer_offsets[] = { gObjectManager->GetBaseOffset(), 0 };

		gGraphics->pRenderer->pPersistentDescriptorSlim->Bind(
			0, gGraphics->GetFrame()->CmdBuffer, pipeline,
			Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
	}

	for (ObjectID object_id : section.Objects) {
		Object* object = gObjectManager->GetObject(object_id);
		if (object == nullptr) {
			continue;
		}

		object->Update();

		const MaterialID material_id = object->GetMaterialID();

		CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

		gGraphics->FrameAlloc.Bind(cmd, pipeline,
								   gGraphics->pRenderer->GetViewConstants(camera, object->GetObjectLayer()));

		DrawPushConstants consts {};
		consts.ObjectId = object->GetDrawIndex();
		consts.MaterialIndex = material_id.GetID();
		consts.BoneBase = object->BoneBufferBase;
		gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex | eShaderType::Pixel, consts);

		if (!gMaterialManager->BindWithPipeline(cmd, pipeline, material_id)) {
			gMaterialManager->BindWithPipeline(cmd, pipeline, MaterialID::scNull);
		}

		object->RenderPrimitive(cmd);
	}
}


void World::AddToRenderListRecursive(renderer::PipelineHandle pipeline, ObjectID* id_ptr)
{
	if (id_ptr == nullptr) {
		return;
	}

	ObjectID id = *id_ptr;

	RenderListSection& section = mRenderList.GetSection(pipeline);

	for (ObjectID object_id : section.Objects) {
		if (object_id == id) {
			return;
		}
	}

	mRenderList.AddObject(pipeline, id);

	Object* obj = gObjectManager->GetObject(id);
	if (obj == nullptr) {
		return;
	}

	for (ObjectID& attached_id : obj->AttachedNodes) {
		AddToRenderListRecursive(pipeline, &attached_id);
	}
}


void World::AddShadowCasterRecursive(ObjectID id)
{
	Object* object = gObjectManager->GetObject(id);
	if (object == nullptr) {
		return;
	}

	if (CastsShadows(*object)) {
		RenderListSection& section = mRenderList.GetSection(GetShadowListPipeline());

		bool is_listed = false;

		for (ObjectID object_id : section.Objects) {
			if (object_id == id) {
				is_listed = true;
				break;
			}
		}

		if (!is_listed) {
			mRenderList.AddObject(GetShadowListPipeline(), id);
		}
	}

	for (ObjectID attached_id : object->AttachedNodes) {
		AddShadowCasterRecursive(attached_id);
	}
}


void World::AddToRenderListRecursiveByMaterial(ObjectID* id_ptr, const Frustum* frustum,
											   const PortalVisibility* portals)
{
	if (id_ptr == nullptr) {
		return;
	}

	ObjectID id = *id_ptr;

	Object* obj = gObjectManager->GetObject(id);
	if (obj == nullptr) {
		return;
	}

	// Container objects (e.g. the root of a multi-primitive mesh) have no mesh/material of their own; only the
	// attached primitives that actually own a mesh need placing in a geometry section.
	if (obj->pMesh.IsValid() && !obj->IsShadowOnly()) {
		bool is_visible = true;

		if (frustum != nullptr && obj->CanBeFrustumCulled()) {
			FrustumTestedObjects++;

			if (obj->IsOutsideFrustum(*frustum)) {
				FrustumCulledObjects++;
				is_visible = false;
			}
		}

		if (is_visible && portals != nullptr && obj->CanBePortalCulled() &&
			!portals->IsVisible(obj->GetAreas(), obj->GetDrawBounds())) {
			PortalCulledObjects++;
			is_visible = false;
		}

		if (is_visible) {
			mRenderList.AddObject(GetPipelineForObject(*obj), obj->ID);
		}
	}

	for (ObjectID& attached_id : obj->AttachedNodes) {
		AddToRenderListRecursiveByMaterial(&attached_id, frustum, portals);
	}
}


void World::ClearRenderList()
{
	for (const ePipelinePass pass : { ePipelinePass::Forward, ePipelinePass::ForwardBlend }) {
		gPipelineCache->ForEachPassPipeline(pass, [&](PipelineHandle pipeline) { mRenderList.ClearSection(pipeline); });
	}

	mRenderList.ClearSection(GetShadowListPipeline());
}

void World::AddTileToRenderList(bool clear, TileIndex new_tile_index, const Frustum* frustum,
								const PortalVisibility* portals)
{
	Tile* tile = gWorldGrid->GetTile(new_tile_index);

	if (tile == nullptr) {
		return;
	}

	uint32 index = 0;
	while (true) {
		index = tile->Objects.SlotsInUse.FindNextSetBit(index);
		if (index == Bitset::scNoFreeBits) {
			break;
		}

		ObjectID* object_id = tile->Objects.GetItem(index);
		if (!object_id) {
			++index;
			continue;
		}

		Object* object = gObjectManager->GetObject(*object_id);
		if (object == nullptr || object->mCullSerial == mCullSerial) {
			++index;
			continue;
		}

		object->mCullSerial = mCullSerial;

		// Markers are drawn as wire boxes instead. Map bounds and portals are the exception: while the editor is
		// showing data brushes they are drawn solid, so that they can be told apart from the geometry around them
#ifdef FX_IS_EDITOR
		const eSpecialMaterial special = object->GetSpecialMaterial();
		const bool shows_bound = (special == eSpecialMaterial::DBound || special == eSpecialMaterial::Portal) &&
								 gEditor->IsDataMode();
#else
		const bool shows_bound = false;
#endif

		if (object->IsVolume() && !shows_bound) {
			++index;
			continue;
		}

		if (object->IsShadowCaster()) {
			AddShadowCasterRecursive(*object_id);
		}

		AddToRenderListRecursiveByMaterial(object_id, frustum, portals);

		++index;
	}
}


void World::AddTileToLightList(TileIndex tile_index, const Frustum* frustum, const PortalVisibility* portals)
{
	const Tile* tile = gWorldGrid->GetTile(tile_index);

	if (tile == nullptr || !tile->Lights.IsInited()) {
		return;
	}

	uint32 index = 0;
	while (true) {
		index = tile->Lights.SlotsInUse.FindNextSetBit(index);
		if (index == Bitset::scNoFreeBits) {
			break;
		}

		const LightID* light_id = tile->Lights.GetItem(index);
		++index;

		if (light_id == nullptr) {
			continue;
		}

		if (frustum != nullptr) {
			const LightBase* light = gLightManager->GetLight(*light_id);

			if (light != nullptr && light->IsOutsideFrustum(*frustum)) {
				continue;
			}
		}

		if (portals != nullptr) {
			const LightBase* light = gLightManager->GetLight(*light_id);

			if (light != nullptr && !portals->IsVisible(light->mAreas, light->GetBounds())) {
				PortalCulledLights++;
				continue;
			}
		}

		mLightList.AddLight(*light_id);
	}
}

void World::AddUnculledLightsToLightList()
{
	const bool needs_all_lights = gProbeManager->IsBaking();

	for (LightBase* light : gLightManager->GetCache()) {
		if (needs_all_lights || light->mTileIndex == TileIndexNull) {
			mLightList.AddLight(light->ID);
		}
	}
}

void World::CullWorldTiles(const PerspectiveCamera& cam)
{
	gObjectManager->FlushInstanceUpdates();

	mFrustum.Rebuild(cam);
	mVisibleTiles.Clear();

	++mCullSerial;

	const bool frustum_cull = (pCVarFrustumCull->IntValue != 0);
	const Frustum* object_frustum = frustum_cull ? &mFrustum : nullptr;
	const Frustum* light_frustum = (frustum_cull && !gProbeManager->IsBaking()) ? &mFrustum : nullptr;

	PortalCulledObjects = 0;
	PortalCulledLights = 0;

	const PortalVisibility* portals = nullptr;

	if (pCVarPortalLock->IntValue == 0) {
		mbPortalLocked = false;
	}
	else if (!mbPortalLocked) {
		mbPortalLocked = true;
		mLockedViewProjection = cam.GetCameraMatrix(eObjectLayer::WorldLayer);
		mLockedCameraPosition = cam.GetPosition();
		mLockedCameraDirection = cam.GetForwardVector();
	}

	if (pCVarPortalCull->IntValue != 0 && !gProbeManager->IsBaking()) {
		if (mbPortalLocked) {
			mPortalVisibility.Update(gWorldGrid->GetAreaGraph(), mLockedViewProjection, mLockedCameraPosition);
		}
		else {
			mPortalVisibility.Update(gWorldGrid->GetAreaGraph(), cam.GetCameraMatrix(eObjectLayer::WorldLayer),
									 cam.GetPosition());
		}
	}
	else {
		mPortalVisibility.Disable();
	}

	if (mPortalVisibility.IsActive()) {
		portals = &mPortalVisibility;
	}

	mLightList.Clear();
	AddTileToLightList(WorldGrid::scGlobalTileIndex, light_frustum, portals);

	BBox frustum_bounds = mFrustum.GetFrustumBoundingBox(cam);

	uint32 num_visible = 0;

	ClearRenderList();

	FrustumTestedObjects = 0;
	FrustumCulledObjects = 0;

	// Super broad phase for culling.

	Vec2u min_tile = gWorldGrid->TileToTileXY(
		gWorldGrid->WorldToTile(Vec3f(frustum_bounds.Min.X, 0.0f, frustum_bounds.Min.Z)));
	Vec2u max_tile = gWorldGrid->TileToTileXY(
		gWorldGrid->WorldToTile(Vec3f(frustum_bounds.Max.X, 0.0f, frustum_bounds.Max.Z)));

	const uint32 num_tiles_x = gWorldGrid->mGridSize.X;
	const uint32 num_tiles_y = gWorldGrid->mGridSize.Y;

	min_tile.X = std::clamp(min_tile.X, 0U, num_tiles_x - 1);
	min_tile.Y = std::clamp(min_tile.Y, 0U, num_tiles_y - 1);

	max_tile.X = std::clamp(max_tile.X, 0U, num_tiles_x - 1);
	max_tile.Y = std::clamp(max_tile.Y, 0U, num_tiles_y - 1);

	for (uint32 y = min_tile.Y; y <= max_tile.Y; y++) {
		for (uint32 x = min_tile.X; x <= max_tile.X; x++) {
			TileIndex ti = gWorldGrid->TileFromTileXY(Vec2u(x, y));

			BBox tile_bounds = gWorldGrid->GetTileAABB(ti);
			tile_bounds.Min.Y = frustum_bounds.Min.Y;
			tile_bounds.Max.Y = frustum_bounds.Max.Y;

			if (!mFrustum.TileIntersectsAABB(tile_bounds)) {
				continue;
			}

			++num_visible;

			mVisibleTiles.Emplace(ti);

			AddTileToRenderList(false, ti, object_frustum, portals);
			AddTileToLightList(ti, light_frustum, portals);
		}
	}

	AddTileToRenderList(false, WorldGrid::scGlobalTileIndex, object_frustum, portals);

	AddUnculledLightsToLightList();
}


void World::Render(BaseCamera* shadow_camera)
{
	PerspectiveCamera& camera = *mpCurrentCamera;
	PerspectiveCamera& cull_camera = GetCullCamera();

	CullWorldTiles(cull_camera);

	TileIndex tile_index = gWorldGrid->WorldToTile(cull_camera.GetPosition());

	if (tile_index != gWorldGrid->ViewTileIndex) {
		gWorldGrid->SetViewTileIndex(tile_index);
	}

	gGraphics->LightBuffer.Rewind();

	// Spot lights need their shadow atlas tile and shadow matrix before they are uploaded
	UpdateSpotShadows();

	mSunLightSlot = UINT32_MAX;

	for (const LightID light_id : mLightList.GetLights()) {
		LightBase* light = gLightManager->GetLight(light_id);

		if (light == nullptr) {
			continue;
		}

		const uint32 slot = gGraphics->LightBuffer.SlotIndex;

		light->Render(camera, shadow_camera);

		// Probe captures swap this slot out for a copy of the sun with its own shadow map, see RenderProbeCapture()
		const bool was_written = (gGraphics->LightBuffer.SlotIndex != slot);

		if (light->Type == eLightType::Directional && was_written && mSunLightSlot == UINT32_MAX) {
			mSunLightSlot = slot;
		}
	}

	// Render shadows into the atlas. The directional light is redrawn every frame, spot lights only when their bake is
	// out of date.
	Ref<LightDirectional> sun = gLightManager->GetDirectionalLight();
	const bool render_sun_shadows = sun.IsValid() && sun->bEnabled;

	gGraphics->bBakedSunActive = render_sun_shadows && (pCVarBakedShadows->IntValue != 0) && pPrototype != nullptr &&
								 pPrototype->IsBakedSunCurrent(sun->GetPosition());

	Vec3f sun_position = Vec3f::sZero;

	if (sun.IsValid()) {
		sun_position = sun->GetPosition();
	}

	gGraphics->bBakedIndirectActive = (pCVarBakedIndirect->IntValue != 0) && pPrototype != nullptr &&
									  pPrototype->IsBakedIndirectCurrent(render_sun_shadows, sun_position);

	// With the sun off its region is left alone, apart from the atlas's very first pass that makes it sampleable
	if (render_sun_shadows || !gShadowAtlas->IsInitialized()) {
		BeginShadowAtlasRegion(gShadowAtlas->GetDirectionalRegion());

		if (render_sun_shadows) {
			ExecuteShadowRenderList(GetShadowListPipeline(), gShadowRenderer->ShadowCamera, gGraphics->bBakedSunActive);
		}

		gShadowAtlas->EndRegion();
	}

	const bool baked_fog_shadow = gGraphics->bBakedSunActive && gGraphics->bHasBakedFogShadow;

	gGraphics->bFarSunShadowActive = render_sun_shadows && !baked_fog_shadow &&
									 (gGraphics->GetFogMode() != eFogMode::Off);

	if (gGraphics->bFarSunShadowActive) {
		BeginShadowAtlasRegion(gShadowAtlas->GetFarDirectionalRegion());
		ExecuteShadowRenderList(GetShadowListPipeline(), gShadowRenderer->FarShadowCamera, false);
		gShadowAtlas->EndRegion();
	}

	BakeSpotShadows();
	gGraphics->MarkGpu(eGpuMarker::Shadows);

	gGraphics->BeginPrepass();

	gPipelineCache->ForEachPassPipeline(ePipelinePass::Forward,
										[&](PipelineHandle pipeline) { ExecutePrepassRenderList(pipeline); });

	gGraphics->pRenderer->Prepass.End();
	gGraphics->MarkGpu(eGpuMarker::Prepass);

	// The decals the camera can see, for the light culling pass to bin
	gDecalManager->Update(camera);

	// Cull lights and decals into screen space tiles before rendering geometry
	gGraphics->BeginLightCulling(camera);
	gGraphics->MarkGpu(eGpuMarker::LightCulling);

	gGraphics->RenderEarlyFrameEffects(camera);
	gGraphics->MarkGpu(eGpuMarker::SSAO);

	gGraphics->BeginGeometry();

	// Opaque first
	ExecuteForwardRenderLists(*mpCurrentCamera);

	// Transparent after, back-to-front globally sorted, depth write disabled
	ExecuteTransparentRenderLists();

#ifdef FX_IS_EDITOR
	if (DebugBoundsMask & scDebugBoundsObjects) {
		DebugDrawObjectBounds();
	}

	if (DebugBoundsMask & scDebugBoundsLights) {
		DebugDrawLightBounds();
	}

	if (DebugBoundsMask & scDebugBoundsPhysics) {
		DebugDrawPhysicsBodies();
	}

	if (bRenderProbes) {
		DebugDrawProbes();
	}

	// Probe volumes are edited as brushes, so the editor always shows them
	if (pCVarShowProbeVolumes->IntValue != 0 || gEditor->IsDataMode()) {
		DebugDrawProbeVolumes();
	}

	DebugDrawSelectedDecals();
#endif

	// DebugDrawWorldGrid();

	DebugDrawPortals();
	DebugDrawCullFrustum();

	// Everything queued this frame, by the calls above or by anything else, is drawn on top of the geometry
	gDebugDraw->Render(gGraphics->GetFrame()->CmdBuffer, camera);
}


/// Fraction of the sun shadow map's half-width that has to lie between a probe and the map's edges for the probe to
/// be captured with it. Past that the shadow map is re-rendered around the probe.
static constexpr float32 scProbeShadowEdgeMargin = 0.5f;

bool World::RenderCaptureSunShadows(LightDirectional& sun, const Vec3f center, OrthoCamera& out_shadow_camera,
									uint32& out_light_slot)
{
	// Same projection as the player's shadow camera, only the placement changes
	OrthoCamera shadow_camera = gShadowRenderer->ShadowCamera;
	gShadowRenderer->PlaceCamera(shadow_camera, center, sun.GetPosition().Normalize());

	const uint32 light_slot = gGraphics->LightBuffer.SlotIndex;

	sun.Render(*mpCurrentCamera, &shadow_camera);

	// The light buffer is full (LightBase::Render() warns about this)
	if (gGraphics->LightBuffer.SlotIndex == light_slot) {
		return false;
	}

	BeginShadowAtlasRegion(gShadowAtlas->GetDirectionalRegion());
	ExecuteShadowRenderList(GetShadowListPipeline(), shadow_camera);
	gShadowAtlas->EndRegion();

	out_shadow_camera = shadow_camera;
	out_light_slot = light_slot;

	return true;
}

void World::RenderProbeCapture()
{
	if (!gProbeManager->IsCapturePending()) {
		return;
	}

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	const Vec2u extent = gProbeManager->GetCaptureExtent();

	const uint32 saved_tile_columns = gGraphics->pRenderer->GetLightTileColumns();
	const uint32 saved_tile_rows = gGraphics->pRenderer->GetLightTileRows();

	// The render list was culled to the player's view, but the probes need to see the whole level
	ClearRenderList();

	++mCullSerial;

	const Vec2u grid_size = gWorldGrid->GetGridSize();

	for (TileIndex tile = 0; tile < grid_size.X * grid_size.Y; tile++) {
		AddTileToRenderList(false, tile);
	}

	AddTileToRenderList(false, WorldGrid::scGlobalTileIndex);

	// The sun's shadow map follows the player and only holds the casters the player can see, so each batch renders
	// its own from every caster in the level, centered on the batch's first probe. Later probes that aren't well
	// inside of it get it re-rendered around them. The main view was already drawn with the player's shadow map, so
	// each re-render goes with a copy of the sun in a spare light slot, which the capture's light culling swaps in.
	Ref<LightDirectional> sun = gLightManager->GetDirectionalLight();
	const bool sun_has_shadows = sun.IsValid() && sun->bEnabled && (mSunLightSlot != UINT32_MAX);

	OrthoCamera capture_shadow_camera;
	bool has_capture_shadows = false;

	// Only the lights from the main view get culled, not the copies of the sun appended after them
	LightCullOverride light_override { .LightCount = gGraphics->LightBuffer.SlotIndex };

	gProbeManager->RecordCaptureBatch(
		cmd,
		[&](PerspectiveCamera& camera, RenderStage& stage)
		{
			const bool needs_sun_shadows = !has_capture_shadows ||
										   !ShadowDirectional::IsWellCovered(
											   capture_shadow_camera, camera.GetPosition(), scProbeShadowEdgeMargin);

			uint32 sun_slot;

			if (sun_has_shadows && needs_sun_shadows &&
				RenderCaptureSunShadows(*sun, camera.GetPosition(), capture_shadow_camera, sun_slot)) {
				light_override.ReplacedLight = mSunLightSlot;
				light_override.ReplacementSlot = sun_slot;
				has_capture_shadows = true;
			}

			// Forward+ culling for this face's camera and extent
			gGraphics->pRenderer->DoLightCullingPass(camera, &extent, &light_override);

			stage.Begin(cmd);

			ExecuteForwardRenderLists(camera);

			stage.End();
		});

	gGraphics->pRenderer->mLightTileColumns = saved_tile_columns;
	gGraphics->pRenderer->mLightTileRows = saved_tile_rows;
}

void World::DebugDrawObjectBounds()
{
	const Color debug_color = Color::FromRGBA(80, 200, 255, 255);

	for (Object& object : gObjectManager->GetCache()) {
		if (object.IsVolume() || object.GetObjectLayer() == eObjectLayer::PlayerLayer) {
			continue;
		}

		gDebugDraw->WireBox(object.GetDrawBounds(), debug_color);
	}
}

void World::DebugDrawLightBounds()
{
	const Color listed_color = Color::FromRGBA(255, 215, 60, 255);
	const Color culled_color = Color::FromRGBA(150, 80, 20, 255);
	const Color disabled_color = Color::FromRGBA(110, 110, 110, 255);

	for (LightBase* light : gLightManager->GetCache()) {
		if (!light->IsCullable()) {
			continue;
		}

		Color color = listed_color;
		if (!light->bEnabled) {
			color = disabled_color;
		}
		else if (!mLightList.Contains(light->ID)) {
			color = culled_color;
		}

		gDebugDraw->WireBox(light->GetBounds(), color);
	}
}


void World::DebugDrawWorldGrid()
{
	const Color debug_color = Color::FromRGBA(255, 255, 255, 255);
	const Color player_debug_color = Color::FromRGBA(255, 0, 0, 255);

	const Vec3f tile_size = Vec3f(gWorldGrid->mTileSize.X, 1.0f, gWorldGrid->mTileSize.Y);

	for (uint32 y = 0; y < gWorldGrid->mGridSize.Y; y++) {
		for (uint32 x = 0; x < gWorldGrid->mGridSize.X; x++) {
			const Vec3f tile_offset = Vec3f(x, -1.5f, y) * tile_size;

			Mat4f model_matrix = Mat4f::AsScale(tile_size) * Mat4f::AsRotation(Quat::scIdentity) *
								 Mat4f::AsTranslation((tile_offset)-gWorldGrid->mPositionOffset + (tile_size * 0.5f));

			Color color = debug_color;

			for (const TileIndex vis_ti : mVisibleTiles) {
				if (vis_ti == gWorldGrid->TileFromTileXY(Vec2u(x, y))) {
					color = player_debug_color;
				}
			}

			gDebugDraw->WireBox(model_matrix, color);
		}
	}
}


void World::DebugDrawPhysicsBodies()
{
	const Color dynamic_color = Color::FromRGBA(100, 255, 40, 255);
	const Color static_color = Color::FromRGBA(40, 120, 30, 255);

	uint32 current_phys_state = gPhysics->UpdateState.load();
	if (mLastPhysicsUpdateState != current_phys_state) {
		mLastPhysicsUpdateState = current_phys_state;
		mCachedPhysicsBodies = gPhysics->CollectBodies();
	}

	for (physics::Body* phys : mCachedPhysicsBodies) {
		const JPH::Body* body = phys->GetBody();
		if (body == nullptr) {
			continue;
		}

		const JPH::AABox bounds = body->GetWorldSpaceBounds();
		const Color color = (phys->GetMotionType() == physics::eMotionType::Static) ? static_color : dynamic_color;

		gDebugDraw->WireBox(BBox(Vec3f(bounds.mMin), Vec3f(bounds.mMax)), color);
	}
}

Object* World::RaycastProbeVolumes(const Vec3f origin, const Vec3f direction, float32 max_distance,
								   float32& out_distance, const std::function<bool(const Object&)>& accept)
{
	Object* nearest = nullptr;
	float32 nearest_distance = max_distance;

	for (Object& object : gObjectManager->GetCache()) {
		if (!object.IsVolume() || (accept != nullptr && !accept(object))) {
			continue;
		}

		Vec3f face;
		const float32 distance = object.RaycastBounds(origin, direction, face);

		if (distance >= 0.0f && distance < nearest_distance) {
			nearest = &object;
			nearest_distance = distance;
		}
	}

	out_distance = nearest_distance;

	return nearest;
}

void World::DebugDrawSelectedDecals()
{
#ifdef FX_IS_EDITOR
	const Color decal_color = Color::FromRGBA(255, 120, 200, 255);

	const auto& selection = gEditor->GetSelection();

	for (uint32 i = 0; i < selection.GetCount(); i++) {
		Object* object = selection.GetObject(i);

		if (object == nullptr || !object->IsDecal()) {
			continue;
		}

		const Vec3f half_extent = (object->Bounds.Max - object->Bounds.Min) * 0.5f;
		const Vec3f center = (object->Bounds.Max + object->Bounds.Min) * 0.5f;

		gDebugDraw->WireBox(Mat4f::AsScale(half_extent) * Mat4f::AsTranslation(center) * object->GetWorldMatrix(),
							decal_color);
	}
#endif
}

void World::DebugDrawProbeVolumes()
{
	const Color volume_color = Color::FromRGBA(60, 220, 255, 255);
	const Color reflection_color = Color::FromRGBA(255, 120, 220, 255);
	const Color trigger_color = Color::FromRGBA(120, 255, 90, 255);
	const Color nvbrush_color = Color::FromRGBA(255, 150, 40, 255);
	const Color selected_color = Color::FromRGBA(255, 220, 60, 255);

	for (Object& object : gObjectManager->GetCache()) {
		if (!object.IsVolume()) {
			continue;
		}

#ifdef FX_IS_EDITOR
		if (gEditor->IsDataMode() && !gEditor->IsObjectSelectable(&object)) {
			continue;
		}

		const bool is_selected = gEditor->GetSelection().Contains(&object);
#else
		const bool is_selected = false;
#endif

		// Map bounds and portals are drawn solid with their own material instead, and only get a wire box to show
		// selection
		const eSpecialMaterial special = object.GetSpecialMaterial();

		if ((special == eSpecialMaterial::DBound || special == eSpecialMaterial::Portal) && !is_selected) {
			continue;
		}

		const Vec3f half_extent = (object.Bounds.Max - object.Bounds.Min) * 0.5f;
		const Vec3f center = (object.Bounds.Max + object.Bounds.Min) * 0.5f;

		Mat4f world_matrix = Mat4f::AsScale(half_extent) * Mat4f::AsTranslation(center) * object.GetWorldMatrix();

		Color color = object.IsReflectionProbe() ? reflection_color : volume_color;

		if (object.IsTrigger() && !object.IsProbeVolume()) {
			color = trigger_color;
		}

		if (object.GetSpecialMaterial() == eSpecialMaterial::NvBrush) {
			color = nvbrush_color;
		}

		gDebugDraw->WireBox(world_matrix, is_selected ? selected_color : color);
	}

	static const Vec3f scCapturePointHalfExtent(0.12f);

	for (uint32 i = 0; i < gProbeManager->GetReflectionProbeCount(); i++) {
		gDebugDraw->SolidBox(gProbeManager->GetReflectionProbePosition(i), scCapturePointHalfExtent, Quat::scIdentity,
							 reflection_color);
	}
}

void World::DebugDrawProbes()
{
	if (gProbeManager == nullptr) {
		return;
	}

	// Tiny solid cubes (~0.15m). The base debug cube spans -1..+1, so scale by half-extent.
	static const Vec3f scProbeHalfExtent(0.1f);

	static const Color scVolumeColors[Limits::MaxProbeVolumes] = {
		Color::FromRGBA(60, 220, 255, 100),	 Color::FromRGBA(255, 220, 60, 100),  Color::FromRGBA(140, 255, 90, 100),
		Color::FromRGBA(255, 110, 200, 100), Color::FromRGBA(170, 140, 255, 100), Color::FromRGBA(90, 255, 210, 100),
		Color::FromRGBA(255, 160, 110, 100), Color::FromRGBA(200, 200, 200, 100),
	};

	const Color capturing_color = Color::FromRGBA(255, 150, 30, 255);

	const bool is_baking = gProbeManager->IsBaking();
	const uint32 current_probe = gProbeManager->GetCurrentProbeIndex();

	for (uint32 volume = 0; volume < gProbeManager->GetVolumeCount(); volume++) {
		uint32 first_probe;
		uint32 num_probes;
		gProbeManager->GetVolumeProbeRange(volume, first_probe, num_probes);

		const Color volume_color = scVolumeColors[volume % std::size(scVolumeColors)];

		for (uint32 i = first_probe; i < first_probe + num_probes; i++) {
			const Color color = (is_baking && i == current_probe) ? capturing_color : volume_color;

			gDebugDraw->SolidBox(gProbeManager->GetProbePosition(i), scProbeHalfExtent, Quat::scIdentity, color);
		}
	}
}

void World::Destroy()
{
	if (pPrototype != nullptr) {
		delete pPrototype;
		pPrototype = nullptr;
	}

	// mPhysicsObjects.Destroy();
}

static Vec3f UnprojectScreenPoint(const Mat4f& inverse_view_projection, float32 x, float32 y, const Vec3f& eye,
								  const Vec3f& forward, float32 distance)
{
	const Vec4f& row_x = inverse_view_projection.Rows[0];
	const Vec4f& row_y = inverse_view_projection.Rows[1];
	const Vec4f& row_z = inverse_view_projection.Rows[2];
	const Vec4f& row_w = inverse_view_projection.Rows[3];

	constexpr float32 depth = 0.5f;

	const float32 w = x * row_x.W + y * row_y.W + depth * row_z.W + row_w.W;
	const Vec3f point((x * row_x.X + y * row_y.X + depth * row_z.X + row_w.X) / w,
					  (x * row_x.Y + y * row_y.Y + depth * row_z.Y + row_w.Y) / w,
					  (x * row_x.Z + y * row_y.Z + depth * row_z.Z + row_w.Z) / w);

	const Vec3f direction = (point - eye).Normalize();

	return eye + direction * (distance / std::max(direction.Dot(forward), 0.1f));
}

PerspectiveCamera& World::GetCullCamera()
{
	if (mpCullCamera.IsValid()) {
		return *mpCullCamera;
	}

	return *mpCurrentCamera;
}

void World::DebugDrawCullFrustum()
{
	if (!mpCullCamera.IsValid()) {
		return;
	}

	const Color frustum_color = Color::FromRGBA(255, 60, 200, 255);

	const PerspectiveCamera& camera = *mpCullCamera;
	const Mat4f inverse_view_projection = camera.GetCameraMatrix(eObjectLayer::WorldLayer).Inverse();

	const float32 distances[] = { 0.25f, 5.0f, 25.0f };
	const Vec2f ndc_corners[] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };

	Vec3f previous_ring[4];

	for (uint32 ring = 0; ring < std::size(distances); ring++) {
		Vec3f corners[4];

		for (uint32 corner = 0; corner < 4; corner++) {
			corners[corner] = UnprojectScreenPoint(inverse_view_projection, ndc_corners[corner].X,
												   ndc_corners[corner].Y, camera.GetPosition(),
												   camera.GetForwardVector(), distances[ring]);
		}

		for (uint32 corner = 0; corner < 4; corner++) {
			gDebugDraw->Line(corners[corner], corners[(corner + 1) % 4], frustum_color);

			if (ring == 0) {
				gDebugDraw->Line(camera.GetPosition(), corners[corner], frustum_color);
			}
			else {
				gDebugDraw->Line(previous_ring[corner], corners[corner], frustum_color);
			}

			previous_ring[corner] = corners[corner];
		}
	}

	gDebugDraw->WireBox(BBox(camera.GetPosition() - Vec3f(0.1f), camera.GetPosition() + Vec3f(0.1f)), frustum_color);
}

void World::DebugDrawPortals()
{
	const int64 debug_level = pCVarPortalDebug->IntValue;
	const AreaGraph& graph = gWorldGrid->GetAreaGraph();

	if (debug_level == 0 || graph.IsEmpty()) {
		return;
	}

	const Color passed_color = Color::FromRGBA(80, 255, 120, 255);
	const Color rejected_color = Color::FromRGBA(255, 80, 60, 255);
	const Color unreached_color = Color::FromRGBA(120, 120, 120, 255);
	const Color closed_color = Color::FromRGBA(255, 150, 40, 255);
	const Color visible_area_color = Color::FromRGBA(255, 215, 60, 255);
	const Color hidden_area_color = Color::FromRGBA(70, 70, 90, 255);
	const Color rect_color = Color::FromRGBA(60, 220, 255, 255);

	constexpr float32 scNormalLength = 0.3f;

	for (uint32 portal_index = 0; portal_index < graph.GetPortalCount(); portal_index++) {
		const MapPortal& portal = graph.GetPortal(portal_index);

		Color color = unreached_color;

		switch (mPortalVisibility.GetPortalState(portal_index)) {
		case ePortalState::Passed:
			color = passed_color;
			break;
		case ePortalState::Rejected:
			color = rejected_color;
			break;
		case ePortalState::Closed:
			color = closed_color;
			break;
		default:
			break;
		}

		if (!portal.bOpen) {
			color = closed_color;
		}

		uint32 first_point = 0;

		for (size_t polygon = 0; polygon < portal.PolygonSizes.Size; polygon++) {
			const uint32 size = portal.PolygonSizes[polygon];
			Vec3f center = Vec3f::sZero;

			for (uint32 point = 0; point < size; point++) {
				const Vec3f& from = portal.Points[first_point + point];
				const Vec3f& to = portal.Points[first_point + (point + 1) % size];

				gDebugDraw->Line(from, to, color);
				center += from;
			}

			center = center * (1.0f / static_cast<float32>(size));

			gDebugDraw->Line(center, center + portal.PolygonPlanes[polygon].Normal * scNormalLength, color);

			first_point += size;
		}
	}

	if (debug_level >= 2) {
		for (uint32 area = 0; area < graph.GetAreaCount(); area++) {
			Color color = hidden_area_color;

			if (mPortalVisibility.IsActive() && mPortalVisibility.IsAreaVisible(area)) {
				color = visible_area_color;
			}

			gDebugDraw->WireBox(graph.GetArea(area).Bounds, color);
		}
	}

	if (!mPortalVisibility.IsActive()) {
		return;
	}

	Vec3f eye = GetCullCamera().GetPosition();
	Vec3f forward = GetCullCamera().GetForwardVector();
	float32 distance = 0.5f;

	if (mbPortalLocked) {
		eye = mLockedCameraPosition;
		forward = mLockedCameraDirection;
		distance = 2.0f;

		gDebugDraw->WireBox(BBox(eye - Vec3f(0.1f), eye + Vec3f(0.1f)), rect_color);
	}

	const Mat4f inverse_view_projection = mPortalVisibility.GetViewProjection().Inverse();

	for (uint32 area = 0; area < graph.GetAreaCount(); area++) {
		if (static_cast<int32>(area) == mPortalVisibility.GetCameraArea() || !mPortalVisibility.IsAreaVisible(area)) {
			continue;
		}

		const ScreenRect& rect = mPortalVisibility.GetAreaRect(area);

		const Vec3f corners[4] = {
			UnprojectScreenPoint(inverse_view_projection, rect.MinX, rect.MinY, eye, forward, distance),
			UnprojectScreenPoint(inverse_view_projection, rect.MaxX, rect.MinY, eye, forward, distance),
			UnprojectScreenPoint(inverse_view_projection, rect.MaxX, rect.MaxY, eye, forward, distance),
			UnprojectScreenPoint(inverse_view_projection, rect.MinX, rect.MaxY, eye, forward, distance),
		};

		for (uint32 corner = 0; corner < 4; corner++) {
			gDebugDraw->Line(corners[corner], corners[(corner + 1) % 4], rect_color);

			if (mbPortalLocked) {
				gDebugDraw->Line(eye, corners[corner], rect_color);
			}
		}
	}
}

} // namespace fx
