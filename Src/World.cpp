#include "World.hpp"

#include <Blockout.hpp>
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
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/LightProbe.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/ShadowAtlas.hpp>
#include <Renderer/ShadowDirectional.hpp>
#include <algorithm>

namespace fx {

using namespace renderer;

void World::Create()
{
	mLights.Create(32);

	SortedEntryBuffer.SetPageSize(128);

	mSpotShadowBakes.SetPageSize(ShadowAtlas::scMaxSpotTiles);
	mSpotShadowCasters.SetPageSize(256);

	mVisibleTiles.InitCapacity(600);
}

static bool IsTransparentMaterial(Material* mat) { return (mat != nullptr && mat->Properties.Alpha < 0.999f); }

/**
 * @brief The geometry pipeline that draws a material, including the transparent variants.
 */
static PipelineHandle GetPipelineForMaterial(const MaterialID& material_id)
{
	Material* material = MaterialManagerFwd::GetMaterial(material_id);

	const ePipelinePass pass = IsTransparentMaterial(material) ? ePipelinePass::ForwardBlend : ePipelinePass::Forward;
	const PipelineHandle pipeline = gPipelineCache->GetOrCreateVariant(pass, material->GetPipelineFeatures());

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

static void AddToRenderListOnce(RenderList& render_list, PipelineHandle pipeline, ObjectID id)
{
	for (ObjectID object_id : render_list.GetSection(pipeline).Objects) {
		if (object_id == id) {
			return;
		}
	}

	render_list.AddObject(pipeline, id);
}

/**
 * @brief Adds the object to the section of each pipeline its materials need. An object drawn in mesh sections can need
 * several.
 */
static void AddToGeometrySections(RenderList& render_list, Object* object)
{
	if (object->MeshSections.IsEmpty()) {
		AddToRenderListOnce(render_list, GetPipelineForMaterial(object->GetMaterialID()), object->ID);
		return;
	}

	for (const MeshSection& section : object->MeshSections) {
		AddToRenderListOnce(render_list, GetPipelineForMaterial(object->GetSectionMaterial(section)), object->ID);
	}
}

/**
 * @brief Calls `draw` for each part of the object that the pipeline draws: the whole mesh (nullptr) for an object
 * without mesh sections, otherwise each section whose material needs that pipeline.
 */
template <typename TDrawFunc>
static void ForEachDrawInPipeline(Object* object, PipelineHandle pipeline, TDrawFunc&& draw)
{
	if (object->MeshSections.IsEmpty()) {
		draw(nullptr);
		return;
	}

	for (const MeshSection& section : object->MeshSections) {
		if (GetPipelineForMaterial(object->GetSectionMaterial(section)) == pipeline) {
			draw(&section);
		}
	}
}


static void AddObjectToRenderList(Object* object, World* scene)
{
	if (object->pMesh.IsValid()) {
		// If the object casts shadows as well, add it to the shadow section
		if (object->IsShadowCaster()) {
			gWorld->mRenderList.AddObject(GetShadowListPipeline(), object->ID);
		}

		AddToGeometrySections(gWorld->mRenderList, object);
	}

	if (!object->AttachedNodes.IsEmpty()) {
		LogInfo("Listing attached nodes for {}:", object->ID.GetID());

		for (const ObjectID& attach_id : object->AttachedNodes) {
			Object* attached_object = gObjectManager->GetObject(attach_id);

			if (object->IsShadowCaster()) {
				attached_object->SetShadowCaster(true);
			}

			LogInfo("   Object {}", attach_id.GetID());
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

	object_ticket.OnLoaded(
		[this](void* item_ptr)
		{
			Object* object = static_cast<Object*>(item_ptr);
			object->bIsAddedToWorld.store(true);
			AddObjectToRenderList(object, this);
			gWorldGrid->AddObject(object->ID);
		});
}

void World::Attach(const Ref<LightBase>& light)
{
	mLights.Insert(light);
	light->OnAttached(this);
}

void World::Detach(ObjectID id)
{
	mRenderList.InvalidateObject(id);
	gWorldGrid->RemoveObject(id);
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

	gPipelineCache->ForEachPassPipeline(
		ePipelinePass::Forward,
		[&](PipelineHandle pipeline)
		{
			const PipelineHandle draw_pipeline =
				is_probe_capture ? gPipelineCache->GetOrCreateVariantInPass(pipeline, ePipelinePass::ForwardCapture)
								 : pipeline;

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

		ForEachDrawInPipeline(object, pipeline_handle,
							  [&](const MeshSection* section) { object->RenderShallow(camera, &pipeline, section); });
	}
}

void World::ExecuteTransparentRenderLists()
{
	PerspectiveCamera& camera = *mpCurrentCamera;
	const Vec3f camera_position = camera.Position;
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

	// Bind per entry as pipeline changes; minimize binds by caching last pipeline
	renderer::Pipeline* pipeline = nullptr;
	PipelineHandle pipeline_handle;
	bool first = true;

	for (const TransparentObjectCarrier& transparent_object : SortedEntryBuffer) {
		// For each differing pipeline (per object), bind the pipeline.
		if (first || !(transparent_object.Pipeline == pipeline_handle)) {
			pipeline = &gPipelineCache->Get(transparent_object.Pipeline);

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

		ForEachDrawInPipeline(object, transparent_object.Pipeline,
							  [&](const MeshSection* section) { object->RenderShallow(camera, pipeline, section); });
	}
}


/**
 * @brief Picks between the opaque and the alpha masked shadow pipeline as casters are drawn into a shadow atlas region,
 * so only alpha masked casters pay for sampling their albedo.
 */
class ShadowPipelineSelector
{
public:
	/// Binds whichever pipeline `object` needs to draw with `material_id` (and its albedo, if masked), returning it for
	/// the push constants.
	Pipeline& Select(Object* object, const MaterialID& material_id, const CommandBuffer& cmd)
	{
		const bool masked = IsMasked(object, material_id);
		const PipelineHandle wanted = gPipelineCache->GetOrCreateVariant(
			ePipelinePass::Shadow, masked ? ePipelineFeatures::AlphaMask : ePipelineFeatures::None);

		if (!(wanted == mBound)) {
			gShadowAtlas->BindPipeline(wanted);
			mBound = wanted;

			const uint32 buffer_offsets[] = { gObjectManager->GetBaseOffset(), 0 };

			gGraphics->pRenderer->pPersistentDescriptorSlim->Bind(
				0, cmd, gPipelineCache->Get(wanted),
				Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
		}

		Pipeline& pipeline = gPipelineCache->Get(mBound);

		if (masked) {
			gMaterialManager->BindWithPipeline(cmd, pipeline, material_id);
		}

		return pipeline;
	}

private:
	static bool IsMasked(Object* object, const MaterialID& material_id)
	{
		if (object->IsSkinned()) {
			return false;
		}

		Material* material = gMaterialManager->GetMaterial(material_id);

		return material != nullptr && HasFlag(material->Properties.Flags, eMaterialFlags::AlphaMask) &&
			   material->IsReady();
	}

private:
	/// Every region starts out with the opaque pipeline bound
	PipelineHandle mBound = gPipelineCache->GetOrCreateVariant(ePipelinePass::Shadow, ePipelineFeatures::None);
};

/**
 * @brief Draws a shadow caster, drawing each of its mesh sections with the pipeline its material needs.
 */
static void RenderShadowCaster(Object* object, ShadowPipelineSelector& selector, const ShadowPushConstants& consts,
							   const CommandBuffer& cmd)
{
	auto Draw = [&](const MeshSection* section)
	{
		const MaterialID material_id = (section != nullptr) ? object->GetSectionMaterial(*section)
															: object->GetMaterialID();

		gGraphics->SubmitPushConstants(cmd, selector.Select(object, material_id, cmd), eShaderType::Vertex, consts);
		object->RenderPrimitive(cmd, section);
	};

	if (object->MeshSections.IsEmpty()) {
		Draw(nullptr);
		return;
	}

	for (const MeshSection& section : object->MeshSections) {
		Draw(&section);
	}
}

void World::ExecuteShadowRenderList(renderer::PipelineHandle pipeline, const Camera& shadow_camera)
{
	const RenderListSection& section = mRenderList.GetSection(pipeline);

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	ShadowPipelineSelector selector;

	// Push constants definition
	ShadowPushConstants consts;
	memcpy(consts.CameraMatrix, shadow_camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(float32) * 16);

	// The sun shadows rendered for a probe capture only come from casters the probes can see, so objects left out of
	// the capture don't shadow it either
	const bool is_probe_capture = gProbeManager->IsCapturingFaces();

	for (ObjectID object_id : section.Objects) {
		if (object_id.IsInvalid()) {
			continue;
		}

		Object* object = gObjectManager->GetObject(object_id);
		if (object == nullptr || (is_probe_capture && !object->IsProbeVisible())) {
			continue;
		}

		object->Update();

		// Push the direct index for the object id
		consts.ObjectIndex = object_id.GetID();
		RenderShadowCaster(object, selector, consts, cmd);
	}
}


void World::UpdateSpotShadows()
{
	mSpotShadowBakes.Clear();
	mSpotShadowCasters.Clear();

	for (const Ref<LightBase>& light_ref : mLights) {
		if (light_ref->Type != eLightType::Spot) {
			continue;
		}

		LightSpot* light = static_cast<LightSpot*>(&(*light_ref));
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
							   ShadowAtlas::scMaxSpotTiles, light->Name.Get());
					sbWarned = true;
				}

				continue;
			}
		}

		const Mat4f shadow_matrix = light->CalculateShadowMatrix();

		const uint32 first_caster = mSpotShadowCasters.Size;
		GatherSpotShadowCasters(light->GetPosition(), light->GetRadius(), mSpotShadowCasters);

		Hash32 bake_hash = HashObj32(gShadowAtlas->GetGeneration());
		bake_hash = HashObj32(shadow.AtlasTile, bake_hash);
		bake_hash = HashObj32(shadow_matrix.RawData, bake_hash);

		for (uint32 index = first_caster; index < mSpotShadowCasters.Size; index++) {
			const ObjectID caster_id = mSpotShadowCasters[index];
			Object* caster = gObjectManager->GetObject(caster_id);

			const PrimitiveMesh* mesh = caster->pMesh.IsValid() ? &(*caster->pMesh) : nullptr;

			// The same test as Object::RenderPrimitive(), so a caster that finishes loading gets baked in
			const bool is_drawable = (mesh != nullptr) && caster->CheckIfReady(false);

			bake_hash = HashObj32(caster_id.GetID(), bake_hash);
			bake_hash = HashObj32(mesh, bake_hash);
			bake_hash = HashObj32(is_drawable, bake_hash);
			bake_hash = HashObj32(caster->GetWorldMatrix().RawData, bake_hash);
		}

		// The tile already holds this exact bake
		if (bake_hash == shadow.BakeHash) {
			while (mSpotShadowCasters.Size > first_caster) {
				mSpotShadowCasters.RemoveLast();
			}

			continue;
		}

		shadow.Matrix = shadow_matrix;
		shadow.BakeHash = bake_hash;

		mSpotShadowBakes.Insert(SpotShadowBake {
			.pLight = light,
			.FirstCaster = first_caster,
			.NumCasters = mSpotShadowCasters.Size - first_caster,
		});
	}
}


void World::BeginShadowAtlasRegion(const ShadowAtlasRegion& region)
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

		memcpy(consts.CameraMatrix, shadow.Matrix.RawData, sizeof(float32) * 16);

		const uint32 end_caster = bake.FirstCaster + bake.NumCasters;

		for (uint32 index = bake.FirstCaster; index < end_caster; index++) {
			const ObjectID caster_id = mSpotShadowCasters[index];

			Object* caster = gObjectManager->GetObject(caster_id);
			if (caster == nullptr) {
				continue;
			}

			// Casters out of view skip the forward pass, which is what normally uploads their matrix for this frame
			gObjectManager->Submit(caster_id, caster->GetWorldMatrix());

			consts.ObjectIndex = caster_id.GetID();
			RenderShadowCaster(caster, selector, consts, cmd);
		}

		gShadowAtlas->EndRegion();
	}
}


void World::GatherSpotShadowCasters(const Vec3f& center, float32 radius, DynArray<ObjectID>& out_casters)
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
				AddSpotShadowCasterRecursive(*object_id, first_caster, out_casters);
			}
		}
	};

	const Vec3f extent(radius, radius, radius);

	const Vec2u min_tile = gWorldGrid->TileToTileXY(gWorldGrid->WorldToTile(center - extent));
	const Vec2u max_tile = gWorldGrid->TileToTileXY(gWorldGrid->WorldToTile(center + extent));

	for (uint32 y = min_tile.Y; y <= max_tile.Y; y++) {
		for (uint32 x = min_tile.X; x <= max_tile.X; x++) {
			add_casters_from_tile(gWorldGrid->TileFromTileXY(Vec2u(x, y)));
		}
	}

	add_casters_from_tile(WorldGrid::scGlobalTileIndex);
}


void World::AddSpotShadowCasterRecursive(ObjectID id, uint32 first_caster, DynArray<ObjectID>& out_casters)
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

	// The shadow pipeline only has the default vertex layout. Skinned meshes would also animate out of the bake.
	if (!object->IsSkinned()) {
		out_casters.Insert(id);
	}

	for (ObjectID& attached_id : object->AttachedNodes) {
		AddSpotShadowCasterRecursive(attached_id, first_caster, out_casters);
	}
}

void World::ExecutePrepassRenderList(renderer::PipelineHandle forward_pipeline)
{
	// Only the opaque pipelines have a prepass one. The blended ones don't write depth, they are blended in forward.
	const PipelineHandle prepass_pipeline = gPipelineCache->GetOrCreateVariantInPass(forward_pipeline, ePipelinePass::Depth);

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

		DrawPushConstants consts { .TargetSize = { gGraphics->Swapchain.Extent.X, gGraphics->Swapchain.Extent.Y } };
		consts.ObjectId = object_id.GetID();
		consts.TileColumns = gGraphics->pRenderer->GetLightTileColumns();
		consts.TileRows = gGraphics->pRenderer->GetLightTileRows();
		consts.BoneBase = object->BoneBufferBase;

		const Mat4f& cam_matrix = camera.GetCameraMatrix(object->GetObjectLayer());
		memcpy(consts.CameraMatrix, cam_matrix.RawData, sizeof(Mat4f));

		ForEachDrawInPipeline(
			object, forward_pipeline,
			[&](const MeshSection* section)
			{
				const MaterialID material_id = (section != nullptr) ? object->GetSectionMaterial(*section)
																	: object->GetMaterialID();

				CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

				consts.MaterialIndex = material_id.GetID();
				gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex | eShaderType::Pixel, consts);

				if (!gMaterialManager->BindWithPipeline(cmd, pipeline, material_id)) {
					gMaterialManager->BindWithPipeline(cmd, pipeline, MaterialID::scNull);
				}

				object->RenderPrimitive(cmd, section);
			});
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


void World::AddToRenderListRecursiveByMaterial(ObjectID* id_ptr)
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
	if (obj->pMesh.IsValid()) {
		AddToGeometrySections(mRenderList, obj);
	}

	for (ObjectID& attached_id : obj->AttachedNodes) {
		AddToRenderListRecursiveByMaterial(&attached_id);
	}
}


void World::ClearRenderList()
{
	for (const ePipelinePass pass : { ePipelinePass::Forward, ePipelinePass::ForwardBlend }) {
		gPipelineCache->ForEachPassPipeline(pass,
											[&](PipelineHandle pipeline) { mRenderList.ClearSection(pipeline); });
	}

	mRenderList.ClearSection(GetShadowListPipeline());
}

void World::AddTileToRenderList(bool clear, TileIndex new_tile_index)
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
		if (object == nullptr) {
			++index;
			continue;
		}

		if (object->IsProbeVolume()) {
			++index;
			continue;
		}

		if (object->IsShadowCaster()) {
			AddToRenderListRecursive(GetShadowListPipeline(), object_id);
		}

		AddToRenderListRecursiveByMaterial(object_id);

		++index;
	}
}


void World::CullWorldTiles(const PerspectiveCamera& cam)
{
	mFrustum.Rebuild(cam);
	mVisibleTiles.Clear();

	AABB frustum_bounds = mFrustum.GetFrustumBoundingBox(cam);

	uint32 num_visible = 0;

	ClearRenderList();

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

			AABB tile_bounds = gWorldGrid->GetTileAABB(ti);

			if (!mFrustum.TileIntersectsAABB(tile_bounds)) {
				continue;
			}

			++num_visible;

			mVisibleTiles.Emplace(ti);

			AddTileToRenderList(false, ti);
		}
	}

	AddTileToRenderList(false, WorldGrid::scGlobalTileIndex);
}


void World::Render(Camera* shadow_camera)
{
	PerspectiveCamera& camera = *mpCurrentCamera;

	CullWorldTiles(camera);

	if (!mpDebugCube.IsValid()) {
		mpDebugCube = MeshGen::MakeCube({})->AsMesh(renderer::eVertexType::Slim);
	}

	TileIndex tile_index = gWorldGrid->WorldToTile(mpCurrentCamera->Position);

	if (tile_index != gWorldGrid->ViewTileIndex) {
		gWorldGrid->SetViewTileIndex(tile_index);
	}

	gGraphics->LightBuffer.Rewind();

	// Spot lights need their shadow atlas tile and shadow matrix before they are uploaded
	UpdateSpotShadows();

	mSunLightSlot = UINT32_MAX;

	for (const Ref<LightBase>& light : mLights) {
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
	Ref<LightDirectional> sun = GetDirectionalLight();
	const bool render_sun_shadows = sun.IsValid() && sun->bEnabled;

	// With the sun off its region is left alone, apart from the atlas's very first pass that makes it sampleable
	if (render_sun_shadows || !gShadowAtlas->IsInitialized()) {
		BeginShadowAtlasRegion(gShadowAtlas->GetDirectionalRegion());

		if (render_sun_shadows) {
			ExecuteShadowRenderList(GetShadowListPipeline(), gShadowRenderer->ShadowCamera);
		}

		gShadowAtlas->EndRegion();
	}

	BakeSpotShadows();

	gGraphics->BeginPrepass();

	gPipelineCache->ForEachPassPipeline(ePipelinePass::Forward,
										[&](PipelineHandle pipeline) { ExecutePrepassRenderList(pipeline); });

	gGraphics->pRenderer->Prepass.End();

	// The decals the camera can see, for the light culling pass to bin
	gDecalManager->Update(camera);

	// Cull lights and decals into screen space tiles before rendering geometry
	gGraphics->BeginLightCulling(camera);

	gGraphics->RenderEarlyFrameEffects(camera);

	gGraphics->BeginGeometry();

	// Opaque first
	ExecuteForwardRenderLists(*mpCurrentCamera);

	// Transparent after, back-to-front globally sorted, depth write disabled
	ExecuteTransparentRenderLists();

	if (bRenderPhysicsObjects) {
		RenderPhysicsObjects(camera);
	}

	if (bRenderProbes) {
		RenderProbeDebug(camera);
	}

#ifdef FX_IS_EDITOR
	// Probe volumes are edited as brushes, so the editor always shows them
	RenderProbeVolumes(camera);
#else
	if (bRenderProbes) {
		RenderProbeVolumes(camera);
	}
#endif

	// RenderWorldGrid(camera);
}


/// Fraction of the sun shadow map's half-width that has to lie between a probe and the map's edges for the probe to
/// be captured with it. Past that the shadow map is re-rendered around the probe.
static constexpr float32 scProbeShadowEdgeMargin = 0.5f;

bool World::RenderCaptureSunShadows(LightDirectional& sun, const Vec3f& center, OrthoCamera& out_shadow_camera,
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

	const Vec2u extent(ProbeManager::scCaptureSize, ProbeManager::scCaptureSize);

	const uint32 saved_tile_columns = gGraphics->pRenderer->GetLightTileColumns();
	const uint32 saved_tile_rows = gGraphics->pRenderer->GetLightTileRows();

	// The render list was culled to the player's view, but the probes need to see the whole level
	ClearRenderList();

	const Vec2u grid_size = gWorldGrid->GetGridSize();

	for (TileIndex tile = 0; tile < grid_size.X * grid_size.Y; tile++) {
		AddTileToRenderList(false, tile);
	}

	AddTileToRenderList(false, WorldGrid::scGlobalTileIndex);

	// The sun's shadow map follows the player and only holds the casters the player can see, so each batch renders
	// its own from every caster in the level, centered on the batch's first probe. Later probes that aren't well
	// inside of it get it re-rendered around them. The main view was already drawn with the player's shadow map, so
	// each re-render goes with a copy of the sun in a spare light slot, which the capture's light culling swaps in.
	Ref<LightDirectional> sun = GetDirectionalLight();
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
										   !ShadowDirectional::IsWellCovered(capture_shadow_camera, camera.Position,
																			 scProbeShadowEdgeMargin);

			uint32 sun_slot;

			if (sun_has_shadows && needs_sun_shadows &&
				RenderCaptureSunShadows(*sun, camera.Position, capture_shadow_camera, sun_slot)) {
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

void World::RenderBoundingBoxes(const Camera& camera)
{
	if (!mpDebugCube.IsValid()) {
		mpDebugCube = MeshGen::MakeCube({})->AsMesh(renderer::eVertexType::Slim);
	}

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	renderer::Pipeline& pipeline = gPipelineCache->Request(ePipelineName::DebugLayer);
	pipeline.Bind(cmd);

	DebugLayerPushConstants push_constants {};

	const Color debug_color = Color::FromRGBA(150, 255, 80, 255);

	for (Object& object : gObjectManager->GetCache()) {
		Mat4f model_matrix = Mat4f::AsScale(object.Bounds.GetSize()) * Mat4f::AsRotation(object.mRotation) *
							 Mat4f::AsTranslation(object.GetPosition() + (object.Bounds.GetSize() / Vec3f(2.0f)) +
												  object.Bounds.Min);

		Mat4f combined_matrix = model_matrix * camera.GetCameraMatrix(eObjectLayer::WorldLayer);
		memcpy(push_constants.CombinedMatrix, combined_matrix.RawData, sizeof(push_constants.CombinedMatrix));

		push_constants.DebugColor = debug_color.AsUInt();

		gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, push_constants);
		mpDebugCube->Render(cmd, 1);
	}
}


void World::RenderWorldGrid(const Camera& camera)
{
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	renderer::Pipeline& pipeline = gPipelineCache->Request(ePipelineName::DebugLayer);
	pipeline.Bind(cmd);


	DebugLayerPushConstants push_constants {};

	const Color debug_color = Color::FromRGBA(255, 255, 255, 255);
	const Color player_debug_color = Color::FromRGBA(255, 0, 0, 255);

	const Vec3f tile_size = Vec3f(gWorldGrid->mTileSize.X, 1.0f, gWorldGrid->mTileSize.Y);

	Vec2u camera_tile_index = gWorldGrid->TileToTileXY(gWorldGrid->ViewTileIndex);

	for (uint32 y = 0; y < gWorldGrid->mGridSize.Y; y++) {
		for (uint32 x = 0; x < gWorldGrid->mGridSize.X; x++) {
			const Vec3f tile_offset = Vec3f(x, -1.5f, y) * tile_size;

			Mat4f model_matrix = Mat4f::AsScale(tile_size) * Mat4f::AsRotation(Quat::scIdentity) *
								 Mat4f::AsTranslation((tile_offset)-gWorldGrid->mPositionOffset + (tile_size * 0.5f));

			Mat4f combined_matrix = model_matrix * camera.GetCameraMatrix(eObjectLayer::WorldLayer);
			memcpy(push_constants.CombinedMatrix, combined_matrix.RawData, sizeof(push_constants.CombinedMatrix));

			push_constants.DebugColor = debug_color.AsUInt();

			for (const TileIndex vis_ti : mVisibleTiles) {
				if (vis_ti == gWorldGrid->TileFromTileXY(Vec2u(x, y))) {
					push_constants.DebugColor = player_debug_color.AsUInt();
				}
			}

			gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, push_constants);
			mpDebugCube->Render(cmd, 1);
		}
	}
}


void World::RenderPhysicsObjects(const Camera& camera)
{
	if (!mpDebugCube.IsValid()) {
		mpDebugCube = MeshGen::MakeCube({})->AsMesh(renderer::eVertexType::Slim);
	}

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;
	// gRenderer->pDeferredRenderer->PlDebugLayer.Bind(cmd);

	renderer::Pipeline& pipeline = gPipelineCache->Request(ePipelineName::DebugLayer);
	pipeline.Bind(cmd);

	DebugLayerPushConstants push_constants {};

	const Color debug_color = Color::FromRGBA(255, 40, 40, 255);
	const Color selected_color = Color::FromRGBA(100, 255, 40, 255);

	uint32 current_phys_state = gPhysics->UpdateState.load();
	if (mLastPhysicsUpdateState != current_phys_state) {
		mLastPhysicsUpdateState = current_phys_state;
		mCachedPhysicsBodies = gPhysics->CollectBodies();
	}


	for (physics::Body* phys : mCachedPhysicsBodies) {
		// As we are using scale here, we want to halve the dimensions
		Mat4f world_matrix = Mat4f::AsScale(phys->Dimensions * 0.5) * Mat4f::AsRotation(phys->GetRotation()) *
							 Mat4f::AsTranslation(phys->GetPosition());
		Mat4f combined_matrix = world_matrix * camera.GetCameraMatrix(eObjectLayer::WorldLayer);


		memcpy(push_constants.CombinedMatrix, combined_matrix.RawData, sizeof(push_constants.CombinedMatrix));

		push_constants.DebugColor = selected_color.AsUInt();

		gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, push_constants);
		mpDebugCube->Render(cmd, 1);
	}
}

Object* World::RaycastProbeVolumes(const Vec3f& origin, const Vec3f& direction, float32 max_distance,
								   float32& out_distance)
{
	Object* nearest = nullptr;
	float32 nearest_distance = max_distance;

	for (Object& object : gObjectManager->GetCache()) {
		if (!object.IsProbeVolume()) {
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

void World::RenderProbeVolumes(const Camera& camera)
{
	if (!mpWireBox.IsValid()) {
		mpWireBox = MeshGen::MakeWireframeBox()->AsMesh(renderer::eVertexType::Slim);
	}

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	renderer::Pipeline& pipeline = gPipelineCache->Request(ePipelineName::DebugLayer);
	pipeline.Bind(cmd);

	DebugLayerPushConstants push_constants {};

	const Color volume_color = Color::FromRGBA(60, 220, 255, 255);
	const Color selected_color = Color::FromRGBA(255, 220, 60, 255);

	for (Object& object : gObjectManager->GetCache()) {
		if (!object.IsProbeVolume()) {
			continue;
		}

		const Vec3f half_extent = (object.Bounds.Max - object.Bounds.Min) * 0.5f;
		const Vec3f center = (object.Bounds.Max + object.Bounds.Min) * 0.5f;

		Mat4f world_matrix = Mat4f::AsScale(half_extent) * Mat4f::AsTranslation(center) * object.GetWorldMatrix();
		Mat4f combined_matrix = world_matrix * camera.GetCameraMatrix(eObjectLayer::WorldLayer);

		memcpy(push_constants.CombinedMatrix, combined_matrix.RawData, sizeof(push_constants.CombinedMatrix));

#ifdef FX_IS_EDITOR
		const bool is_selected = gEditor->GetSelection().Contains(&object);
#else
		const bool is_selected = false;
#endif
		push_constants.DebugColor = is_selected ? selected_color.AsUInt() : volume_color.AsUInt();

		gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, push_constants);
		mpWireBox->Render(cmd, 1);
	}
}

void World::RenderProbeDebug(const Camera& camera)
{
	if (!mpDebugCube.IsValid()) {
		mpDebugCube = MeshGen::MakeCube({})->AsMesh(renderer::eVertexType::Slim);
	}

	if (gProbeManager == nullptr) {
		return;
	}

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	renderer::Pipeline& pipeline = gPipelineCache->Request(ePipelineName::DebugSolid);
	pipeline.Bind(cmd);

	DebugLayerPushConstants push_constants {};

	// Tiny solid cubes (~0.15m). The base debug cube spans -1..+1, so scale by half-extent.
	static const Vec3f scProbeHalfExtent(0.1f);

	static const Color scVolumeColors[Limits::MaxProbeVolumes] = {
		Color::FromRGBA(60, 220, 255, 100),	 Color::FromRGBA(255, 220, 60, 100),  Color::FromRGBA(140, 255, 90, 100),
		Color::FromRGBA(255, 110, 200, 100), Color::FromRGBA(170, 140, 255, 100), Color::FromRGBA(90, 255, 210, 100),
		Color::FromRGBA(255, 160, 110, 100), Color::FromRGBA(200, 200, 200, 100),
	};

	const Color capturing_color = Color::FromRGBA(255, 150, 30, 255);
	const Color inactive_color = Color::FromRGBA(255, 40, 40, 60);

	const bool is_baking = gProbeManager->IsBaking();
	const uint32 current_probe = gProbeManager->GetCurrentProbeIndex();

	for (uint32 volume = 0; volume < gProbeManager->GetVolumeCount(); volume++) {
		uint32 first_probe;
		uint32 num_probes;
		gProbeManager->GetVolumeProbeRange(volume, first_probe, num_probes);

		const uint32 volume_color = scVolumeColors[volume % std::size(scVolumeColors)].AsUInt();

		for (uint32 i = first_probe; i < first_probe + num_probes; i++) {
			Mat4f world_matrix = Mat4f::AsScale(scProbeHalfExtent) *
								 Mat4f::AsTranslation(gProbeManager->GetProbePosition(i));
			Mat4f combined_matrix = world_matrix * camera.GetCameraMatrix(eObjectLayer::WorldLayer);

			memcpy(push_constants.CombinedMatrix, combined_matrix.RawData, sizeof(push_constants.CombinedMatrix));

			if (is_baking && i == current_probe) {
				push_constants.DebugColor = capturing_color.AsUInt();
			}
			else {
				push_constants.DebugColor = gProbeManager->IsProbeActive(i) ? volume_color : inactive_color.AsUInt();
			}

			gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, push_constants);
			mpDebugCube->Render(cmd, 1);
		}
	}
}

void World::Destroy()
{
	mLights.Destroy();

	if (pBlockout != nullptr) {
		delete pBlockout;
		pBlockout = nullptr;
	}

	// mPhysicsObjects.Destroy();
}

} // namespace fx
