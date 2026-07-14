#include "Scene.hpp"

#include <Engine.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManagerFwd.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/PipelineCache.hpp>
#include <Renderer/RenderBackend.hpp>
#include <Renderer/ShadowDirectional.hpp>

namespace fx {

using namespace renderer;

void Scene::Create()
{
	mObjects.Create(32);
	mLights.Create(32);
	mPhysicsObjects.Create(32);
}

void Scene::Attach(Object* object)
{
	mObjects.Insert(object->ID);
	gWorldGrid->Insert(object->ID);

	object->pScene = this;
	object->OnAttached(this);

	Material* material = MaterialManagerFwd::GetMaterial(object->GetMaterialID());
	object->pScene->mRenderList.Add(material->GetPipelineName(), object->ID);

	for (const ObjectID& attach_id : object->AttachedNodes) {
		Object* attach = gObjectManager->GetObject(attach_id);
		material = MaterialManagerFwd::GetMaterial(attach->GetMaterialID());
		object->pScene->mRenderList.Add(material->GetPipelineName(), attach_id);
	}
}

static void AddObjectToRenderList(Object* object, Scene* scene)
{
	if (object->pScene == nullptr) {
		object->pScene = scene;
	}

	if (object->pMesh.IsValid()) {
		const bool is_unlit = object->IsUnlit();

		Material* material = MaterialManagerFwd::GetMaterial(object->GetMaterialID());
		if (is_unlit) {
			LogInfo("Setting material {} pipeline to be unlit", material->ID);
			material->SetPipeline(ePipelineName::Unlit);
		}

		ePipelineName pipeline_name = material->GetPipelineName();

		AssertMsg(object->pScene, "Scene has not been initialized on object!");
		object->pScene->mRenderList.Add(pipeline_name, object->ID);
	}

	if (!object->AttachedNodes.IsEmpty()) {
		LogInfo("Listing attached nodes for {}:", object->ID.GetID());

		for (const ObjectID& attach_id : object->AttachedNodes) {
			LogInfo("   Object {}", attach_id.GetID());
			AddObjectToRenderList(gObjectManager->GetObject(attach_id), scene);
		}
	}
}


void Scene::Attach(AssetTicket<Object> object_ticket)
{
	Object* object = object_ticket.Get();

	mObjects.Insert(object->ID);

	object->pScene = this;
	object->OnAttached(this);

	object_ticket.OnLoaded(
		[this](void* item_ptr)
		{
			Object* object = static_cast<Object*>(item_ptr);
			AddObjectToRenderList(object, this);
			gWorldGrid->Insert(object->ID);
		});
}

void Scene::Attach(const Ref<LightBase>& light)
{
	mLights.Insert(light);
	light->OnAttached(this);
}

PhObjectId Scene::NewPhysicsObject()
{
	PhObjectId id = mPhysicsObjects.Size();
	PhObject* phys = mPhysicsObjects.Insert();
	phys->SetId(id);

	return id;
}

PhObject* Scene::GetPhysicsObject(PhObjectId id) const
{
	if (id == PhObjectIdNull || id > mPhysicsObjects.Size()) {
		return nullptr;
	}

	return &mPhysicsObjects[id];
}

void Scene::SelectPhysicsObject(const JPH::BodyID& body_id)
{
	for (const PhObject& phys : mPhysicsObjects) {
		if (phys.mpPhysicsBody->GetID() == body_id) {
			mSelectedPhysicsObjectId = phys.GetId();
			return;
		}
	}
}

Object* Scene::FindObject(const Hash32 name_hash)
{
	for (ObjectID& obj_id : mObjects) {
		Object* obj = gObjectManager->GetObject(obj_id);
		if (obj->Name == name_hash) {
			return obj;
		}
	}

	return nullptr;
}

PhObject* Scene::FindPhysicsObject(const Hash32 name_hash)
{
	for (PhObject& phys : mPhysicsObjects) {
		if (phys.GetName().GetHash() == name_hash) {
			return &phys;
		}
	}

	return nullptr;
}


void Scene::ExecuteRenderList(renderer::ePipelineName pl_name)
{
	PerspectiveCamera& camera = *mpCurrentCamera;

	const RenderListSection& section = mRenderList.GetSection(pl_name);

	if (!section.InUse.IsInited()) {
		return;
	}

	// If the pipeline passed in is unlit, force the unlit pipeline to be used over the materials pipeline.
	const bool force_unlit_pipeline = (pl_name == ePipelineName::Unlit);
	renderer::Pipeline* alt_pipeline = (force_unlit_pipeline ? &gPipelineCache->Request(ePipelineName::Unlit)
															 : nullptr);

	uint32 index = 0;
	while (true) {
		index = section.InUse.FindNextSetBit(index);
		if (index == Bitset::scNoFreeBits) {
			break;
		}

		ObjectID object_id = section.Objects[index];
		Object* object = gObjectManager->GetObject(object_id);

		object->Update();
		object->RenderShallow(camera, alt_pipeline);

		++index;
	}
}

void Scene::AddToRenderListRecursive(renderer::ePipelineName pl_name, ObjectID* id_ptr)
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

	Material* material = MaterialManagerFwd::GetMaterial(obj->GetMaterialID());

	// if (material->QualityLevel > 0) {
	// 	material->RequestQuality(material->QualityLevel - 1);
	// }

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

void Scene::RebuildRenderList(bool clear, TileIndex new_tile_index)
{
	Tile* tile = gWorldGrid->GetTile(new_tile_index);

	if (tile == nullptr) {
		return;
	}

	if (clear) {
		CLEAR_RL_SECTION(ePipelineName::Geometry);
		CLEAR_RL_SECTION(ePipelineName::GeometryNormalMaps);
		CLEAR_RL_SECTION(ePipelineName::GeometrySkinned);
		CLEAR_RL_SECTION(ePipelineName::Unlit);
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

		LogInfo("Adding object ID {} -> {}", *object_id, PipelineNameUtil::GetName(material->GetPipelineName()));

		AddToRenderListRecursive(material->GetPipelineName(), object_id);

		++index;
	}
}

void Scene::RebuildFromTiles(TileIndex tile_index)
{
	/*
		+--------+--------+--------+-----
		|		 |        |        |
		| -1,  1 |  0,  1 |  1,  1 |  ...
		|		 |        |        |
		+--------+--------+--------+-----
		|		 |        |        |
		| -1,  0 | PLAYER |  1,  0 |  ...
		|		 |        |        |
		+--------+--------+--------+-----
		|		 |        |        |
		| -1, -1 |  0, -1 |  1, -1 |  ...
		|		 |        |        |
		+--------+--------+--------+-----
		| ...    |  ...   |  ...   |
	*/

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


void Scene::Render(Camera* shadow_camera)
{
	PerspectiveCamera& camera = *mpCurrentCamera;

	if (!mpDebugCube.IsValid()) {
		mpDebugCube = MeshGen::MakeCube({})->AsMesh(renderer::eVertexType::Slim);
	}

	TileIndex tile_index = gWorldGrid->GetTileIndex(mpCurrentCamera->Position);

	if (tile_index != mCameraTileIndex) {
		// RebuildFromTiles(tile_index);
		mCameraTileIndex = tile_index;
	}

	gRenderer->BeginGeometry();

	ExecuteRenderList(ePipelineName::Geometry);
	ExecuteRenderList(ePipelineName::GeometryNormalMaps);
	ExecuteRenderList(ePipelineName::GeometrySkinned);

	// Render lights
	gRenderer->BeginLighting();
	gRenderer->LightBuffer.Rewind();

	for (const Ref<LightBase>& light : mLights) {
		light->Render(camera, shadow_camera);
	}

	// Render the unlit objects
	gRenderer->BeginUnlit();

	ExecuteRenderList(ePipelineName::Unlit);

	// RenderBoundingBoxes(camera);
	RenderWorldGrid(camera);

	if (bRenderPhysicsObjects) {
		RenderPhysicsObjects(camera);
	}
}


void Scene::RenderBoundingBoxes(const Camera& camera)
{
	if (!mpDebugCube.IsValid()) {
		mpDebugCube = MeshGen::MakeCube({})->AsMesh(renderer::eVertexType::Slim);
	}

	CommandBuffer& cmd = gRenderer->GetFrame()->CmdBuffer;

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

		gRenderer->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, push_constants);
		mpDebugCube->Render(cmd, 1);
	}
}


void Scene::RenderWorldGrid(const Camera& camera)
{
	CommandBuffer& cmd = gRenderer->GetFrame()->CmdBuffer;

	renderer::Pipeline& pipeline = gPipelineCache->Request(ePipelineName::DebugLayer);
	pipeline.Bind(cmd);


	DebugLayerPushConstants push_constants {};

	const Color debug_color = Color::FromRGBA(255, 255, 30, 255);
	const Color player_debug_color = Color::FromRGBA(0, 255, 255, 255);

	const Vec3f tile_size = Vec3f(gWorldGrid->mTileSize.X, 1.0f, gWorldGrid->mTileSize.Y);

	Vec2u camera_tile_index = gWorldGrid->GetTileXY(mCameraTileIndex);

	for (uint32 y = 0; y < gWorldGrid->mGridSize.Y; y++) {
		for (uint32 x = 0; x < gWorldGrid->mGridSize.X; x++) {
			const Vec3f tile_offset = Vec3f(x, -1.5f, y) * tile_size;

			Mat4f model_matrix = Mat4f::AsScale(tile_size) * Mat4f::AsRotation(Quat::sIdentity) *
								 Mat4f::AsTranslation((tile_offset)-gWorldGrid->mPositionOffset + (tile_size * 0.5f));

			Mat4f combined_matrix = model_matrix * camera.GetCameraMatrix(eObjectLayer::WorldLayer);
			memcpy(push_constants.CombinedMatrix, combined_matrix.RawData, sizeof(push_constants.CombinedMatrix));

			push_constants.DebugColor = debug_color.AsUInt();

			if ((x >= camera_tile_index.X - 1 && x <= camera_tile_index.X + 1) &&
				(y >= camera_tile_index.Y - 1 && y <= camera_tile_index.Y + 1)) {
				push_constants.DebugColor = player_debug_color.AsUInt();
			}

			gRenderer->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, push_constants);
			mpDebugCube->Render(cmd, 1);
		}
	}
}


void Scene::RenderPhysicsObjects(const Camera& camera)
{
	if (!mpDebugCube.IsValid()) {
		mpDebugCube = MeshGen::MakeCube({})->AsMesh(renderer::eVertexType::Slim);
	}

	CommandBuffer& cmd = gRenderer->GetFrame()->CmdBuffer;
	// gRenderer->pDeferredRenderer->PlDebugLayer.Bind(cmd);

	renderer::Pipeline& pipeline = gPipelineCache->Request(ePipelineName::DebugLayer);
	pipeline.Bind(cmd);

	DebugLayerPushConstants push_constants {};

	const Color debug_color = Color::FromRGBA(255, 40, 40, 255);
	const Color selected_color = Color::FromRGBA(100, 255, 40, 255);

	for (PhObject& phys : mPhysicsObjects) {
		Mat4f model_matrix = Mat4f::AsScale(phys.Dimensions) * Mat4f::AsRotation(phys.GetRotation()) *
							 Mat4f::AsTranslation(phys.GetPosition());
		Mat4f combined_matrix = model_matrix * camera.GetCameraMatrix(eObjectLayer::WorldLayer);


		memcpy(push_constants.CombinedMatrix, combined_matrix.RawData, sizeof(push_constants.CombinedMatrix));
		if (phys.GetId() == mSelectedPhysicsObjectId) {
			push_constants.DebugColor = selected_color.AsUInt();
		}
		else {
			push_constants.DebugColor = debug_color.AsUInt();
		}

		gRenderer->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, push_constants);
		mpDebugCube->Render(cmd, 1);
	}
}

void Scene::RenderObjectShadows(Object* object)
{
	ShadowPushConstants consts;

	memcpy(consts.CameraMatrix, gShadowRenderer->ShadowCamera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData,
		   sizeof(float32) * 16);

	bool in_skinned_shader = false;

	Pipeline& pipeline = gPipelineCache->Request(ePipelineName::ShadowDirectional);

	CommandBuffer& cmd = gRenderer->GetFrame()->CmdBuffer;

	if (in_skinned_shader && !object->IsSkinned()) {
		in_skinned_shader = false;
		pipeline.Bind(cmd);

		gObjectManager->mObjectBufferDS.BindWithOffset(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline,
													   gObjectManager->GetBaseOffset());
	}

	object->Update();

	consts.ObjectId = object->ID.GetID();

	gRenderer->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, consts);

	object->RenderPrimitive(cmd);

	for (const ObjectID& attached : object->AttachedNodes) {
		RenderObjectShadows(gObjectManager->GetObject(attached));
	}
}


void Scene::RenderShadows(Camera* shadow_camera)
{
	gShadowRenderer->Begin();

	for (const ObjectID& object_id : mObjects) {
		Object* object = gObjectManager->GetObject(object_id);

		if (!object->IsShadowCaster()) {
			continue;
		}

		RenderObjectShadows(object);
	}


	gShadowRenderer->End();
}

void Scene::Destroy()
{
	mObjects.Destroy();
	mLights.Destroy();

	mPhysicsObjects.Destroy();
}

} // namespace fx
