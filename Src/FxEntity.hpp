#pragma once


#include <Asset/FxModel.hpp>
#include <Asset/FxImage.hpp>

#include "FxMaterial.hpp"
#include "Renderer/Backend/RvkFrameData.hpp"
#include "Renderer/Backend/RvkPipeline.hpp"

#include <Math/Vector.hpp>
#include <Math/Mat4.hpp>

#include <Core/FxPagedArray.hpp>

#include <Renderer/FxCamera.hpp>
#include <sys/types.h>

class FxEntity;

class FxScript
{
public:
    FxScript()
    {
    }

    virtual void Init() = 0;

    virtual void RenderTick() = 0;
    virtual void PhysicsTick() = 0;

    virtual void Destroy() = 0;

    virtual ~FxScript()
    {
    }

public:
    FxEntity* Entity = nullptr;
};

class FxEntity
{
public:
    FxEntity()
    {
        Children.reserve(4);
    }

    void AddChild(FxRef<FxEntity>& entity)
    {
        Children.push_back(entity);
    }

    void Translate(const FxVec3f& offset)
    {
        mPosition += offset;
        mModelMatrix = Mat4f::AsTranslation(mPosition);
    }

    void Rotate(const FxVec3f& rotation)
    {

    }

    void Update()
    {
        if (mScript) {
            mScript->RenderTick();
        }
    }

    void AttachScript(FxRef<FxScript> script)
    {
        mScript = script;
        mScript->Entity = this;
    }

    const FxVec3f& GetPosition() const
    {
        return mPosition;
    }

    Mat4f mModelMatrix = Mat4f::Identity;
protected:
    FxVec3f mPosition = FxVec3f::Zero;
    FxVec3f mRotation = FxVec3f::Zero;


    FxRef<FxScript> mScript{nullptr};

    std::vector<FxRef<FxEntity>> Children;
};

class FxEntityManager
{
public:
    static FxEntityManager& GetGlobalManager();

    void Create(uint32 entities_per_page=64);

    static FxRef<FxEntity> New();

private:
    FxPagedArray<FxEntity> mEntityPool;
};

class FxSceneObject : public FxEntity {
public:
    FxSceneObject() = default;

    // void AttachScript(std::unique_ptr<FxScript>& script)
    // {
    //     mScript = std::move(script);
    // }

    void Render(const FxCamera& camera)
    {
        RvkFrameData* frame = Renderer->GetFrame();

        if (!mMaterial || !mMaterial->IsBuilt) {
            mMaterial->Build();
        }

        VkDescriptorSet sets_to_bind[] = {
            frame->DescriptorSet.Set,
            mMaterial->mDescriptorSet
        };

        Mat4f VP = camera.VPMatrix;
        Mat4f MVP = mModelMatrix * VP;

        memcpy(mUbo.MvpMatrix.RawData, MVP.RawData, sizeof(Mat4f));

        frame->SubmitUbo(mUbo);

        RvkDescriptorSet::BindMultiple(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *mMaterial->Pipeline, sets_to_bind, sizeof(sets_to_bind) / sizeof(sets_to_bind[0]));

        DrawPushConstants push_constants{};
        memcpy(push_constants.MVPMatrix, MVP.RawData, sizeof(Mat4f));
        memcpy(push_constants.ModelMatrix, mModelMatrix.RawData, sizeof(Mat4f));

        vkCmdPushConstants(frame->CommandBuffer.CommandBuffer, mMaterial->Pipeline->Layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);


        // if (mMaterial) {
        //     mMaterial->Bind(nullptr);
        // }

        mModel->Render(*mMaterial->Pipeline);
    }

    void Attach(const FxRef<FxMaterial>& material)
    {
        mMaterial = material;
    }

    void Attach(const FxRef<FxModel>& model, RvkGraphicsPipeline& pipeline)
    {
        mModel = model;

        // If there has been no previous materials attached, attach
        // the material from the model.
        // model->OnLoaded([&](FxRef<FxBaseAsset> asset) {
        //     FxModel* mp = static_cast<FxModel*>(asset.Get());
        //     if (this->mMaterial != nullptr) {
        //         return;
        //     }

        //     if (mp->Materials.size() > 0) {
        //         printf("setting to material\n");
        //         this->mMaterial = mp->Materials[0];
        //         this->mMaterial->Pipeline = &pipeline;
        //     }
        // });
    }

    void Attach(const FxRef<FxScript>& script)
    {
        // this->AttachScript(script);
        this->mScript = script;
        this->mScript->Entity = this;
    }

private:
    RvkUniformBufferObject mUbo;

    FxRef<FxModel> mModel{nullptr};
    FxRef<FxMaterial> mMaterial{nullptr};
};
