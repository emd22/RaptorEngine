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
    
    void MoveTo(const FxVec3f& position)
    {
        mPosition = position;
        mModelMatrix = FxMat4f::AsScale(mScale) * FxMat4f::AsTranslation(mPosition);
    }

    void MoveBy(const FxVec3f& offset)
    {
        mPosition += offset;
        mModelMatrix = FxMat4f::AsScale(mScale) * FxMat4f::AsTranslation(mPosition);
    }

    void Scale(const FxVec3f& scale)
    {
        mScale *= scale;
        mModelMatrix = FxMat4f::AsScale(mScale) * mModelMatrix;
    }

    void RotateX(float rad)
    {
        mRotation.X += rad;
        mModelMatrix = FxMat4f::AsRotationX(mRotation.X);
        mModelMatrix.Print();
    }
    
    void RotateY(float rad)
    {
        mRotation.Y += rad;
        mModelMatrix = FxMat4f::AsRotationY(mRotation.Y);
    }
    
    void RotateZ(float rad)
    {
        mRotation.Z += rad;
        mModelMatrix = FxMat4f::AsRotationZ(mRotation.Z);
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

    FxMat4f mModelMatrix = FxMat4f::Identity;
public:
    FxVec3f mPosition = FxVec3f::Zero;
    FxVec3f mRotation = FxVec3f::Zero;
    FxVec3f mScale = FxVec3f::One;


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

    void Render(const FxCamera& camera);

    FX_FORCE_INLINE void Attach(const FxRef<FxMaterial>& material)
    {
        mMaterial = material;
    }

    void Attach(const FxRef<FxModel>& model)
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
