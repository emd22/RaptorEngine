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

	mTileSystem.Create(Vec2u(10, 10));
}

void Scene::Attach(Object* object)
{
	mObjects.Insert(object->ID);
	mTileSystem.Insert(object->ID);

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
	mTileSystem.Insert(object->ID);

	object->pScene = this;
	object->OnAttached(this);

	object_ticket.OnLoaded(
		[this](void* item_ptr)
		{
			Object* object = static_cast<Object*>(item_ptr);
			AddObjectToRenderList(object, this);
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

void Scene::RebuildRenderList(TileIndex new_tile_index)
{
	Tile* tile = mTileSystem.GetTile(new_tile_index);

	RenderListSection& rl = mRenderList.GetSection(ePipelineName::Geometry);
	rl.InUse.ClearAll();
	rl.Objects.Clear();

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

		LogInfo("Adding object ID {}", *object_id);

		mRenderList.Add(ePipelineName::Geometry, *object_id);

		++index;
	}
}


void Scene::Render(Camera* shadow_camera)
{
	PerspectiveCamera& camera = *mpCurrentCamera;

	TileIndex tile_index = mTileSystem.GetTileIndex(mpCurrentCamera->Position);
	if (tile_index != mCameraTileIndex) {
		RebuildRenderList(tile_index);
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

	if (bRenderPhysicsObjects) {
		RenderPhysicsObjects(camera);
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

	// push_constants.ObjectId = ObjectId;

	// memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(mObjectLayer).RawData, sizeof(Mat4f));

	// push_constants.MaterialIndex = 0;

	// vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, pMaterial->pPipeline->Layout,
	//                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push_constants),
	//                    &push_constants);


	// for (const PhObject& obj : mPhysicsObjects) {
	//     mpDebugCube->Render(cmd, 1);
	// }
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
	// if (obj->IsSkinned()) {
	//     pipeline = &gShadowRenderer->GetSkinnedPipeline();
	//     in_skinned_shader = true;
	//     pipeline->Bind(cmd);

	//     gObjectManager->mObjectBufferDS.BindWithOffset(0, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline,
	//                                                    gObjectManager->GetBaseOffset());
	// }

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
