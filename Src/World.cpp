#include "World.hpp"

#include <Blockout.hpp>
#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Material/MaterialManagerFwd.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Physics/PhysicsManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/LightProbe.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/ShadowDirectional.hpp>
#include <algorithm>

namespace fx {

using namespace renderer;

void World::Create()
{
	mObjects.Create(80);
	mLights.Create(32);

	SortedEntryBuffer.SetPageSize(128);
}

static ePipelineName GetTransparentPipeline(ePipelineName base)
{
	switch (base) {
	case ePipelineName::Geometry:
		return ePipelineName::GeometryTransparent;
	case ePipelineName::GeometryNormalMaps:
		return ePipelineName::GeometryNormalMapsTransparent;
	case ePipelineName::GeometrySkinned:
		return ePipelineName::GeometrySkinnedTransparent;
	default:
		return base;
	}
}
static bool IsTransparentMaterial(Material* mat) { return (mat != nullptr && mat->Properties.Alpha < 0.999f); }


static void AddObjectToRenderList(Object* object, World* scene)
{
	if (object->pMesh.IsValid()) {
		Material* material = gMaterialManager->GetMaterial(object->GetMaterialID());
		ePipelineName pipeline_name = material->GetRequiredPipeline();


		if (IsTransparentMaterial(material)) {
			pipeline_name = GetTransparentPipeline(pipeline_name);
		}

		// if (gWorld->mRenderList.GetObjectIndex(pipeline_name, object->ID) != RenderList::scNotFound) {
		// 	return;
		// }

		LogInfo("Adding Object '{}' to renderlist pipeline {}", object->Name.Get(),
				PipelineNameUtil::GetName(pipeline_name));

		// If the object casts shadows as well, add it to the shadow section
		if (object->IsShadowCaster()) {
			gWorld->mRenderList.Add(ePipelineName::ShadowDirectional, object->ID);
		}

		gWorld->mRenderList.Add(pipeline_name, object->ID);
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

static void RemoveObjectFromRenderList(ObjectID id)
{
	if (id.IsNull() || id.IsInvalid()) {
		return;
	}


	Object* object = gObjectManager->GetObject(id);
	gWorld->mRenderList.RemoveAllOfObject(id);

	if (!object->AttachedNodes.IsEmpty()) {
		for (const ObjectID& attach_id : object->AttachedNodes) {
			RemoveObjectFromRenderList(attach_id);
		}
	}
}


void World::Attach(AssetTicket object_ticket)
{
	Object* object = static_cast<Object*>(object_ticket.Get());

	mObjects.Insert(object->ID);

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
	gWorldGrid->RemoveObject(id);
	RemoveObjectFromRenderList(id);
}

// physics::BodyID World::NewPhysicsObject()
// {
// 	physics::BodyID id = mPhysicsObjects.Size();
// 	physics::Body* phys = mPhysicsObjects.Insert();
// 	phys->SetID(id);

// 	return id;
// }

// physics::Body* World::GetPhysicsObject(physics::BodyID id) const
// {
// 	if (id == physics::BodyID::scNull || id.GetID() > mPhysicsObjects.Size()) {
// 		return nullptr;
// 	}

// 	return &mPhysicsObjects[id.GetID()];
// }

// void World::SelectPhysicsObject(const JPH::BodyID& body_id)
// {
// 	for (const physics::Body& phys : mPhysicsObjects) {
// 		if (phys.mpPhysicsBody->GetID() == body_id) {
// 			mSelectedPhysicsObjectId = phys.GetID();
// 			return;
// 		}
// 	}
// }

Object* World::FindObject(const Hash32 name_hash)
{
	for (ObjectID& obj_id : mObjects) {
		Object* obj = gObjectManager->GetObject(obj_id);
		if (obj->Name == name_hash) {
			return obj;
		}
	}

	return nullptr;
}

// physics::Body* World::FindPhysicsObject(const Hash32 name_hash)
// {
// 	for (physics::Body& phys : mPhysicsObjects) {
// 		if (phys.GetName().GetHash() == name_hash) {
// 			return &phys;
// 		}
// 	}

// 	return nullptr;
// }


void World::ExecuteRenderList(renderer::ePipelineName pl_name)
{
	PerspectiveCamera& camera = *mpCurrentCamera;
	ExecuteRenderList(pl_name, camera);
}

void World::ExecuteRenderList(renderer::ePipelineName pl_name, PerspectiveCamera& camera)
{
	RenderListSection& section = mRenderList.GetSection(pl_name);

	if (!section.InUse.IsInited()) {
		return;
	}


	renderer::Pipeline& pipeline = gPipelineCache->Request(pl_name);

	pipeline.Bind(gGraphics->GetFrame()->CmdBuffer);

	{
		const uint32 buffer_offsets[] = {
			gObjectManager->GetBaseOffset(),	  0,
			gGraphics->GetLightGridFrameOffset(), gGraphics->GetLightIndexListFrameOffset(),
			gGraphics->GetProbeFrameOffset(),	  gGraphics->GetProbeVolumeFrameOffset()
		};

		gGraphics->pRenderer->pPersistentDescriptor->Bind(
			0, gGraphics->GetFrame()->CmdBuffer, pipeline,
			Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
	}


	uint32 index = 0;

	while (true) {
		index = section.InUse.FindNextSetBit(index);

		if (index == Bitset::scNoFreeBits) {
			break;
		}

		ObjectID object_id = section.Objects[index];
		Object* object = gObjectManager->GetObject(object_id);

		object->Update();
		object->RenderShallow(camera, &pipeline);

		++index;
	}
}

void World::ExecuteTransparentRenderLists()
{
	PerspectiveCamera& camera = *mpCurrentCamera;
	const Vec3f camera_position = camera.Position;
	;

	auto Gather = [&](ePipelineName pl_name)
	{
		const RenderListSection& section = mRenderList.GetSection(pl_name);

		if (!section.InUse.IsInited()) {
			return;
		}

		uint32 index = 0;

		while (true) {
			index = section.InUse.FindNextSetBit(index);

			if (index == Bitset::scNoFreeBits) {
				break;
			}

			{
				ObjectID object_id = section.Objects[index];
				Object* object = gObjectManager->GetObject(object_id);

				Vec3f center = object->GetPosition() + object->Bounds.Min + object->Bounds.GetSize() * 0.5f;
				Vec3f diff = center - camera_position;

				float32 distance_sq = diff.Dot(diff);

				SortedEntryBuffer.Insert({ object_id, pl_name, distance_sq });
			}

			++index;
		}
	};

	SortedEntryBuffer.Clear();

	// We store everything in one buffer as the order matters here. The furthest objects should be rendered first.

	Gather(ePipelineName::GeometryTransparent);
	Gather(ePipelineName::GeometryNormalMapsTransparent);
	Gather(ePipelineName::GeometrySkinnedTransparent);

	if (SortedEntryBuffer.Size == 0) {
		return;
	}

	std::sort(SortedEntryBuffer.pData, SortedEntryBuffer.pData + SortedEntryBuffer.Size,
			  [](const TransparentObjectCarrier& a, const TransparentObjectCarrier& b)
			  { return a.Distance > b.Distance; });

	// Bind per entry as pipeline changes; minimize binds by caching last pipeline
	renderer::Pipeline* pipeline = nullptr;
	ePipelineName pipeline_name = ePipelineName::GeometryTransparent;
	bool first = true;

	for (const TransparentObjectCarrier& transparent_object : SortedEntryBuffer) {
		// For each differing pipeline (per object), bind the pipeline.
		if (first || transparent_object.Pipeline != pipeline_name) {
			pipeline = &gPipelineCache->Request(transparent_object.Pipeline);
			pipeline->Bind(gGraphics->GetFrame()->CmdBuffer);

			{
				const uint32 buffer_offsets[] = {
					gObjectManager->GetBaseOffset(),	  0,
					gGraphics->GetLightGridFrameOffset(), gGraphics->GetLightIndexListFrameOffset(),
					gGraphics->GetProbeFrameOffset(),	  gGraphics->GetProbeVolumeFrameOffset()
				};

				gGraphics->pRenderer->pPersistentDescriptor->Bind(
					0, gGraphics->GetFrame()->CmdBuffer, *pipeline,
					Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
			}

			pipeline_name = transparent_object.Pipeline;
			first = false;
		}

		Object* object = gObjectManager->GetObject(transparent_object.ID);

		object->Update();
		object->RenderShallow(camera, pipeline);
	}
}

// void World::ExecuteTransparentRenderLists()
// {
// 	PerspectiveCamera& camera = *mpCurrentCamera;
// 	const Vec3f camPos = camera.Position;

// 	struct SortedEntry
// 	{
// 		ObjectID id;
// 		ePipelineName pipeline;
// 		float distSq;
// 	};

// 	// Precompute total transparent count for correctly sized array
// 	size_t total_transparent = 0;

// 	for (ePipelineName pl : { ePipelineName::GeometryTransparent, ePipelineName::GeometryNormalMapsTransparent,
// 							  ePipelineName::GeometrySkinnedTransparent }) {
// 		const RenderListSection& sec = mRenderList.GetSection(pl);
// 		if (sec.InUse.IsInited()) {
// 			total_transparent += sec.Objects.Size;
// 		}
// 	}

// 	if (total_transparent == 0) {
// 		return;
// 	}

// 	SizedArray<SortedEntry> sorted;
// 	sorted.InitCapacity(total_transparent + 4);

// 	auto Gather = [&](ePipelineName pl_name)
// 	{
// 		const RenderListSection& section = mRenderList.GetSection(pl_name);

// 		if (!section.InUse.IsInited()) {
// 			return;
// 		}

// 		uint32 index = 0;

// 		while (true) {
// 			index = section.InUse.FindNextSetBit(index);
// 			if (index == Bitset::scNoFreeBits)
// 				break;
// 			ObjectID oid = section.Objects[index];
// 			Object* obj = gObjectManager->GetObject(oid);
// 			Vec3f center = =->GetPosition() + obj->Bounds.Min + obj->Bounds.GetSize() * 0.5f;
// 			Vec3f diff = center - camPos;
// 			float d2 = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
// 			sorted.Insert(SortedEntry { oid, pl_name, d2 });
// 			++index;
// 		}
// 	};

// 	Gather(ePipelineName::GeometryTransparent);
// 	Gather(ePipelineName::GeometryNormalMapsTransparent);
// 	Gather(ePipelineName::GeometrySkinnedTransparent);

// 	if (sorted.Size == 0)
// 		return;

// 	std::sort(sorted.pData, sorted.pData + sorted.Size,
// 			  [](const SortedEntry& a, const SortedEntry& b) { return a.distSq > b.distSq; });

// 	// Bind per entry as pipeline changes; minimize binds by caching last pipeline
// 	renderer::Pipeline* currentPipeline = nullptr;
// 	ePipelineName currentPlName = ePipelineName::GeometryTransparent;
// 	bool first = true;

// 	for (const SortedEntry& e : sorted) {
// 		if (first || e.pipeline != currentPlName) {
// 			currentPipeline = &gPipelineCache->Request(e.pipeline);
// 			currentPipeline->Bind(gGraphics->GetFrame()->CmdBuffer);
// 			{
// 				const uint32 buffer_offsets[] = { gObjectManager->GetBaseOffset(), 0,
// 												  gGraphics->GetLightGridFrameOffset(),
// 												  gGraphics->GetLightIndexListFrameOffset() };
// 				gGraphics->pRenderer->pPersistentDescriptor->Bind(
// 					0, gGraphics->GetFrame()->CmdBuffer, *currentPipeline,
// 					Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
// 			}
// 			currentPlName = e.pipeline;
// 			first = false;
// 		}
// 		Object* object = gObjectManager->GetObject(e.id);
// 		object->Update();
// 		object->RenderShallow(camera, currentPipeline);
// 	}
// }


void World::ExecuteShadowRenderList(renderer::ePipelineName pl_name)
{
	PerspectiveCamera& camera = *mpCurrentCamera;

	const RenderListSection& section = mRenderList.GetSection(pl_name);

	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	if (!section.InUse.IsInited()) {
		return;
	}

	renderer::Pipeline& pipeline = gPipelineCache->Request(pl_name);

	// Push constants definition
	ShadowPushConstants consts;
	memcpy(consts.CameraMatrix, gShadowRenderer->ShadowCamera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData,
		   sizeof(float32) * 16);

	uint32 index = 0;
	while (true) {
		index = section.InUse.FindNextSetBit(index);
		if (index == Bitset::scNoFreeBits) {
			break;
		}


		ObjectID object_id = section.Objects[index];
		Object* object = gObjectManager->GetObject(object_id);

		// Push the direct index for the object id
		consts.ObjectIndex = object_id.GetID();
		gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, consts);

		object->Update();
		object->RenderPrimitive(cmd);

		++index;
	}
}

void World::ExecutePrepassRenderList(renderer::ePipelineName forward_pl_name)
{
	// Skip prepass for transparent variants – they don't write depth and are blended in forward
	if (forward_pl_name == ePipelineName::GeometryTransparent ||
		forward_pl_name == ePipelineName::GeometryNormalMapsTransparent ||
		forward_pl_name == ePipelineName::GeometrySkinnedTransparent) {
		return;
	}

	PerspectiveCamera& camera = *mpCurrentCamera;

	const RenderListSection& section = mRenderList.GetSection(forward_pl_name);

	if (!section.InUse.IsInited()) {
		return;
	}

	ePipelineName prepass_pl_name = forward_pl_name;
	switch (forward_pl_name) {
	case ePipelineName::Geometry:
		prepass_pl_name = ePipelineName::DepthNormal;
		break;
	case ePipelineName::GeometryNormalMaps:
		prepass_pl_name = ePipelineName::DepthNormalNormalMaps;
		break;
	case ePipelineName::GeometrySkinned:
		prepass_pl_name = ePipelineName::DepthNormalSkinned;
		break;
	default:
		return;
	}

	renderer::Pipeline& pipeline = gPipelineCache->Request(prepass_pl_name);

	pipeline.Bind(gGraphics->GetFrame()->CmdBuffer);

	{
		const uint32 buffer_offsets[] = { gObjectManager->GetBaseOffset(), 0 };

		gGraphics->pRenderer->pPersistentDescriptorSlim->Bind(
			0, gGraphics->GetFrame()->CmdBuffer, pipeline,
			Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
	}

	const Mat4f& cam_matrix = camera.GetCameraMatrix(eObjectLayer::WorldLayer);

	uint32 index = 0;
	while (true) {
		index = section.InUse.FindNextSetBit(index);
		if (index == Bitset::scNoFreeBits) {
			break;
		}

		ObjectID object_id = section.Objects[index];
		Object* object = gObjectManager->GetObject(object_id);

		object->Update();

		DrawPushConstants consts { .TargetSize = { gGraphics->Swapchain.Extent.X, gGraphics->Swapchain.Extent.Y } };
		consts.ObjectId = object_id.GetID();
		consts.MaterialIndex = object->GetMaterialID().GetID();
		consts.TileColumns = gGraphics->pRenderer->GetLightTileColumns();

		memcpy(consts.CameraMatrix, cam_matrix.RawData, sizeof(Mat4f));

		gGraphics->SubmitPushConstants(gGraphics->GetFrame()->CmdBuffer, pipeline,
									   eShaderType::Vertex | eShaderType::Pixel, consts);

		if (!gMaterialManager->BindWithPipeline(gGraphics->GetFrame()->CmdBuffer, pipeline, object->GetMaterialID())) {
			gMaterialManager->BindWithPipeline(gGraphics->GetFrame()->CmdBuffer, pipeline, MaterialID::scNull);
		}

		object->RenderPrimitive(gGraphics->GetFrame()->CmdBuffer);

		++index;
	}
}


void World::AddToRenderListRecursive(renderer::ePipelineName pl_name, ObjectID* id_ptr)
{
	if (id_ptr == nullptr) {
		return;
	}

	ObjectID id = *id_ptr;

	RenderListSection& section = mRenderList.GetSection(pl_name);

	uint32 index = 0;
	while (true) {
		index = section.InUse.FindNextSetBit(index);
		if (index == Bitset::scNoFreeBits) {
			break;
		}

		const ObjectID* object_id = section.Objects.Get(index);
		if (!object_id) {
			break;
		}

		if ((*object_id) == id) {
			LogInfo("Avoiding ID {}, {}", *object_id, id);
			return;
		}

		++index;
	}


	LogInfo("Adding object to render list index {}", section.Objects.Size);
	mRenderList.Add(pl_name, id);

	Object* obj = gObjectManager->GetObject(id);

	for (ObjectID& attached_id : obj->AttachedNodes) {
		AddToRenderListRecursive(pl_name, &attached_id);
	}
}

#define CLEAR_RL_SECTION(pl_name_)                                                                                     \
	{                                                                                                                  \
		RenderListSection& rl = mRenderList.GetSection(pl_name_);                                                      \
		rl.InUse.ClearAll();                                                                                           \
		rl.Objects.Clear();                                                                                            \
	}

void World::RebuildRenderList(bool clear, TileIndex new_tile_index)
{
	Tile* tile = gWorldGrid->GetTile(new_tile_index);

	if (tile == nullptr) {
		return;
	}

	if (clear) {
		CLEAR_RL_SECTION(ePipelineName::Geometry);
		CLEAR_RL_SECTION(ePipelineName::GeometryNormalMaps);
		CLEAR_RL_SECTION(ePipelineName::GeometrySkinned);
		CLEAR_RL_SECTION(ePipelineName::GeometryTransparent);
		CLEAR_RL_SECTION(ePipelineName::GeometryNormalMapsTransparent);
		CLEAR_RL_SECTION(ePipelineName::GeometrySkinnedTransparent);
		CLEAR_RL_SECTION(ePipelineName::ShadowDirectional);
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
		Material* material = MaterialManagerFwd::GetMaterial(object->GetMaterialID());
		ePipelineName pipeline = material->GetRequiredPipeline();
		if (IsTransparentMaterial(material)) {
			pipeline = GetTransparentPipeline(pipeline);
		}

		LogInfo("Adding object ID {} -> {}", *object_id, PipelineNameUtil::GetName(pipeline));

		if (object->IsShadowCaster()) {
			AddToRenderListRecursive(ePipelineName::ShadowDirectional, object_id);
		}

		AddToRenderListRecursive(pipeline, object_id);

		++index;
	}
}

void World::RebuildFromTiles(TileIndex tile_index)
{
	RebuildRenderList(true, tile_index);
	Vec2u xy = gWorldGrid->GetTileXY(tile_index);

	// Rebuild the immediate surrounding tiles (up, left, down, right)
	RebuildRenderList(false, gWorldGrid->GetTileIndexXY(xy + Vec2u(1, 0)));
	RebuildRenderList(false, gWorldGrid->GetTileIndexXY(xy + Vec2u(-1, 0)));
	RebuildRenderList(false, gWorldGrid->GetTileIndexXY(xy + Vec2u(0, -1)));
	RebuildRenderList(false, gWorldGrid->GetTileIndexXY(xy + Vec2u(0, 1)));

	// Build the diagonals
	RebuildRenderList(false, gWorldGrid->GetTileIndexXY(xy + Vec2u(1, 1)));
	RebuildRenderList(false, gWorldGrid->GetTileIndexXY(xy + Vec2u(1, -1)));
	RebuildRenderList(false, gWorldGrid->GetTileIndexXY(xy + Vec2u(-1, -1)));
	RebuildRenderList(false, gWorldGrid->GetTileIndexXY(xy + Vec2u(-1, 1)));
}

void World::NotifyObjectMaterialChanged(ObjectID id)
{
	if (id.IsNull() || id.IsInvalid()) {
		return;
	}

	Object* object = gObjectManager->GetObject(id);

	if (object == nullptr || !object->pMesh.IsValid()) {
		return;
	}

	Material* material = gMaterialManager->GetMaterial(object->GetMaterialID());

	// Check to see if the object is in the correct pipeline
	// if (mRenderList.GetObjectIndex(material->GetRequiredPipeline(), object->ID) != RenderList::scNotFound) {
	// 	// The object is in the expected rl section, break
	// 	return;
	// }

	RemoveObjectFromRenderList(id);
	AddObjectToRenderList(object, this);
}


void World::Render(Camera* shadow_camera)
{
	PerspectiveCamera& camera = *mpCurrentCamera;

	if (!mpDebugCube.IsValid()) {
		mpDebugCube = MeshGen::MakeCube({})->AsMesh(renderer::eVertexType::Slim);
	}

	TileIndex tile_index = gWorldGrid->GetTileIndex(mpCurrentCamera->Position);

	if (tile_index != gWorldGrid->ViewTileIndex) {
		// RebuildFromTiles(tile_index);
		gWorldGrid->SetViewTileIndex(tile_index);
	}

	gGraphics->LightBuffer.Rewind();

	for (const Ref<LightBase>& light : mLights) {
		light->Render(camera, shadow_camera);
	}

	// Render shadows
	{
		gShadowRenderer->Begin();

		Pipeline& pipeline = gPipelineCache->Request(ePipelineName::ShadowDirectional);
		pipeline.Bind(gGraphics->GetFrame()->CmdBuffer);

		{
			const uint32 buffer_offsets[] = { gObjectManager->GetBaseOffset(), 0 };

			gGraphics->pRenderer->pPersistentDescriptorSlim->Bind(
				0, gGraphics->GetFrame()->CmdBuffer, pipeline,
				Slice<const uint32>(buffer_offsets, std::size(buffer_offsets)));
		}


		ExecuteShadowRenderList(ePipelineName::ShadowDirectional);

		gShadowRenderer->End();
	}

	gGraphics->BeginPrepass();

	ExecutePrepassRenderList(ePipelineName::Geometry);
	ExecutePrepassRenderList(ePipelineName::GeometryNormalMaps);
	ExecutePrepassRenderList(ePipelineName::GeometrySkinned);

	gGraphics->pRenderer->Prepass.End();

	// Cull lights into screen space tiles before rendering geometry
	gGraphics->BeginLightCulling(camera);

	gGraphics->RenderEarlyFrameEffects(camera);

	gGraphics->BeginGeometry();

	// Opaque first
	ExecuteRenderList(ePipelineName::Geometry);
	ExecuteRenderList(ePipelineName::GeometryNormalMaps);
	ExecuteRenderList(ePipelineName::GeometrySkinned);

	// Transparent after, back-to-front globally sorted, depth write disabled
	ExecuteTransparentRenderLists();

	// RenderPhysicsObjects(camera);
}


void World::RenderProbeCapture()
{
	if (gProbeManager == nullptr || !gProbeManager->IsCapturePending()) {
		return;
	}

	gProbeManager->EnsureCaptureStage();

	RenderStage& stage = gProbeManager->GetCaptureStage();
	CommandBuffer& cmd = gGraphics->GetFrame()->CmdBuffer;

	const Vec2u extent(ProbeManager::scCaptureSize, ProbeManager::scCaptureSize);

	// The opaque geometry pipelines default to a swapchain-sized viewport.
	// Override them for the capture extent (restored below).
	static const renderer::ePipelineName scCapturePipelines[] = {
		renderer::ePipelineName::Geometry,
		renderer::ePipelineName::GeometryNormalMaps,
		renderer::ePipelineName::GeometrySkinned,
	};

	Vec2u saved_viewports[std::size(scCapturePipelines)];
	bool saved_fullscreen[std::size(scCapturePipelines)];

	for (uint32 i = 0; i < std::size(scCapturePipelines); i++) {
		renderer::Pipeline& pipeline = gPipelineCache->Request(scCapturePipelines[i]);
		saved_viewports[i] = pipeline.ViewportSize;
		saved_fullscreen[i] = pipeline.bIsViewportFullscreen;

		pipeline.ViewportSize = extent;
		pipeline.bIsViewportFullscreen = false;
	}

	RequirePipelineDynamicStates();

	const uint32 saved_tile_columns = gGraphics->pRenderer->GetLightTileColumns();
	const Vec3f capture_pos = gProbeManager->GetCapturePosition();

	LogInfo("Probe capture: {} faces at {} ({} lights)", ProbeManager::scCaptureFaces, capture_pos,
			gGraphics->LightBuffer.SlotIndex);

	static const Vec3f scFaceDirs[ProbeManager::scCaptureFaces] = {
		Vec3f(1.0f, 0.0f, 0.0f),  Vec3f(-1.0f, 0.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f),
		Vec3f(0.0f, -1.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f),	Vec3f(0.0f, 0.0f, -1.0f),
	};
	static const Vec3f scFaceUps[ProbeManager::scCaptureFaces] = {
		Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f),
		Vec3f(0.0f, 0.0f, 1.0f), Vec3f(0.0f, 1.0f, 0.0f), Vec3f(0.0f, 1.0f, 0.0f),
	};

	for (uint32 face = 0; face < ProbeManager::scCaptureFaces; face++) {
		PerspectiveCamera face_camera;
		face_camera.SetFov(90.0f);
		face_camera.SetAspectRatio(1.0f);
		// NOTE: this engine uses reversed-Z with swapped plane parameters:
		// the "near" value is the far clip distance and vice versa (see the
		// Camera defaults of near=1000/far=0.01 and the weapon camera). Passing
		// them in normal order inverts depth so far surfaces win over near ones.
		face_camera.SetNearPlane(100.0f);
		face_camera.SetFarPlane(0.1f);
		face_camera.UpdateProjectionMatrix();

		// NOTE: Camera::UpdateViewMatrix() hardcodes Vec3f::sUp, which is
		// degenerate for the +/-Y faces, so the view matrix is built directly
		// with a per-face up vector. FinishCaptureBake unprojects through these
		// same matrices, so no convention needs to match anything else.
		face_camera.MoveTo(capture_pos);
		face_camera.ViewMatrix.LookAt(capture_pos, capture_pos + scFaceDirs[face], scFaceUps[face]);
		face_camera.UpdateCameraMatrix();

		// NOTE: face_camera.Update() is intentionally not called: it would
		// rebuild the view matrix with the hardcoded +Y up vector.
		gProbeManager->SetCaptureCamera(face, face_camera);

		// Re-run Forward+ culling for the capture extent + face camera.
		gGraphics->pRenderer->DoLightCullingPass(face_camera, &extent);

		stage.Begin(cmd);

		// Opaque only: transparents are skipped for capture bakes.
		ExecuteRenderList(renderer::ePipelineName::Geometry, face_camera);
		ExecuteRenderList(renderer::ePipelineName::GeometryNormalMaps, face_camera);
		ExecuteRenderList(renderer::ePipelineName::GeometrySkinned, face_camera);

		stage.End();

		gProbeManager->CopyCaptureFaceToStaging(cmd, face);
	}

	for (uint32 i = 0; i < std::size(scCapturePipelines); i++) {
		renderer::Pipeline& pipeline = gPipelineCache->Request(scCapturePipelines[i]);
		pipeline.ViewportSize = saved_viewports[i];
		pipeline.bIsViewportFullscreen = saved_fullscreen[i];
	}

	gGraphics->pRenderer->mLightTileColumns = saved_tile_columns;

	// Force the composition pass to re-emit viewport state for its own size.
	RequirePipelineDynamicStates();

	gProbeManager->MarkCaptureReady();
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

	for (ObjectID object_id : mObjects) {
		Object* object = gObjectManager->GetObject(object_id);

		Mat4f model_matrix = Mat4f::AsScale(object->Bounds.GetSize()) * Mat4f::AsRotation(object->mRotation) *
							 Mat4f::AsTranslation(object->GetPosition() + (object->Bounds.GetSize() / Vec3f(2.0f)) +
												  object->Bounds.Min);

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

	const Color debug_color = Color::FromRGBA(255, 255, 30, 255);
	const Color player_debug_color = Color::FromRGBA(0, 255, 255, 255);

	const Vec3f tile_size = Vec3f(gWorldGrid->mTileSize.X, 1.0f, gWorldGrid->mTileSize.Y);

	Vec2u camera_tile_index = gWorldGrid->GetTileXY(gWorldGrid->ViewTileIndex);

	for (uint32 y = 0; y < gWorldGrid->mGridSize.Y; y++) {
		for (uint32 x = 0; x < gWorldGrid->mGridSize.X; x++) {
			const Vec3f tile_offset = Vec3f(x, -1.5f, y) * tile_size;

			Mat4f model_matrix = Mat4f::AsScale(tile_size) * Mat4f::AsRotation(Quat::scIdentity) *
								 Mat4f::AsTranslation((tile_offset)-gWorldGrid->mPositionOffset + (tile_size * 0.5f));

			Mat4f combined_matrix = model_matrix * camera.GetCameraMatrix(eObjectLayer::WorldLayer);
			memcpy(push_constants.CombinedMatrix, combined_matrix.RawData, sizeof(push_constants.CombinedMatrix));

			push_constants.DebugColor = debug_color.AsUInt();

			if ((x >= camera_tile_index.X - 1 && x <= camera_tile_index.X + 1) &&
				(y >= camera_tile_index.Y - 1 && y <= camera_tile_index.Y + 1)) {
				push_constants.DebugColor = player_debug_color.AsUInt();
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

void World::Destroy()
{
	mObjects.Destroy();
	mLights.Destroy();

	if (pBlockout != nullptr) {
		delete pBlockout;
		pBlockout = nullptr;
	}

	// mPhysicsObjects.Destroy();
}

} // namespace fx
