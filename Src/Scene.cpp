#include "Scene.hpp"

#include <Engine.hpp>
#include <ObjectManager.hpp>
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

void Scene::Attach(const TSRef<Object>& object)
{
    mObjects.Insert(object);
    object->pScene = this;
    object->OnAttached(this);
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

TSRef<Object> Scene::FindObject(const Hash32 name_hash)
{
    for (TSRef<Object>& obj : mObjects) {
        if (obj->Name == name_hash) {
            return obj;
        }
    }

    return TSRef<Object>(nullptr);
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


void Scene::Render(Camera* shadow_camera)
{
    PerspectiveCamera& camera = *mpCurrentCamera;

    gRenderer->BeginGeometry();


    for (const TSRef<Object>& obj : mObjects) {
        obj->Update();


        if (obj->GetRenderUnlit()) {
            continue;
        }

        obj->Render(camera);
    }

    // Render lights
    gRenderer->BeginLighting();

    gRenderer->LightBuffer.Rewind();

    for (const Ref<LightBase>& light : mLights) {
        light->Render(camera, shadow_camera);
    }

    RenderUnlitObjects(camera);

    if (bRenderPhysicsObjects) {
        RenderPhysicsObjects(camera);
    }
}


void Scene::RenderUnlitObjects(const Camera& camera) const
{
    // gRenderer->pDeferredRenderer->PlUnlit.Bind(gRenderer->GetFrame()->CommandBuffer);
    gRenderer->BeginUnlit();

    for (const TSRef<Object>& obj : mObjects) {
        if (!obj->GetRenderUnlit()) {
            continue;
        }
        obj->RenderUnlit(camera);
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

void Scene::RenderObjectShadows(const TSRef<Object>& obj)
{
    ShadowPushConstants consts;

    memcpy(consts.CameraMatrix, gShadowRenderer->ShadowCamera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData,
           sizeof(float32) * 16);

    bool in_skinned_shader = false;

    Pipeline& pipeline = gPipelineCache->Request(ePipelineName::ShadowDirectional);

    CommandBuffer& cmd = gRenderer->GetFrame()->CmdBuffer;

    if (in_skinned_shader && !obj->IsSkinned()) {
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

    obj->Update();

    consts.ObjectId = obj->ObjectId;

    gRenderer->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, consts);

    obj->RenderPrimitive(cmd);

    for (const TSRef<Object>& attached : obj->AttachedNodes) {
        RenderObjectShadows(attached);
    }
}


void Scene::RenderShadows(Camera* shadow_camera)
{
    gShadowRenderer->Begin();

    for (const TSRef<Object>& obj : mObjects) {
        if (!obj->IsShadowCaster()) {
            continue;
        }

        RenderObjectShadows(obj);
    }


    gShadowRenderer->End();
}

void Scene::Destroy()
{
    mObjects.Destroy();
    mLights.Destroy();
}

} // namespace fx
