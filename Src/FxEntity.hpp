#pragma once


#include <Asset/FxModel.hpp>
#include <Asset/FxImage.hpp>

#include "FxMaterial.hpp"

#include <Math/Vector.hpp>

#include <Math/Mat4.hpp>

#include <Core/FxPagedArray.hpp>

class FxScript
{
public:
    virtual void Init() = 0;

    virtual void FrameTick() = 0;
    virtual void PhysicsTick() = 0;

    virtual void Destroy();
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
    }

private:
    FxVec3f mPosition = FxVec3f::Zero;
    FxVec3f mRotation = FxVec3f::Zero;

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

    void Render()
    {
        RvkFrameData* frame = Renderer->GetFrame();
        VkDescriptorSet sets_to_bind[] = {
            frame->DescriptorSet.Set,
            mMaterial->mDescriptorSet
        };

        RvkDescriptorSet::BindMultiple(frame->CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *mMaterial->Pipeline, sets_to_bind, sizeof(sets_to_bind) / sizeof(sets_to_bind[0]));

        // if (mMaterial) {
        //     mMaterial->Bind(nullptr);
        // }
        mModel->Render(*mMaterial->Pipeline);
    }


    void Attach(const FxRef<FxMaterial>& material)
    {
        mMaterial = material;
    }

    void Attach(const FxRef<FxModel>& model)
    {
        mModel = model;
    }

private:
    FxRef<FxModel> mModel{nullptr};
    FxRef<FxMaterial> mMaterial{nullptr};
};
