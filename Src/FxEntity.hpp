#pragma once

#include "FxMaterial.hpp"
#include "Renderer/Backend/RxFrameData.hpp"
#include "Renderer/Backend/RxPipeline.hpp"

#include <sys/types.h>

#include <Asset/FxAssetImage.hpp>
#include <Core/FxPagedArray.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vector.hpp>
#include <Renderer/FxCamera.hpp>

class FxEntity;

class FxScript
{
public:
    FxScript() {}

    virtual void Init() = 0;

    virtual void RenderTick() = 0;
    virtual void PhysicsTick() = 0;

    virtual void Destroy() = 0;

    virtual ~FxScript() {}

public:
    FxEntity* Entity = nullptr;
};

class FxEntity
{
public:
    FxEntity() { Children.reserve(4); }

    void AddChild(FxRef<FxEntity>& entity) { Children.push_back(entity); }

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

    const FxVec3f& GetPosition() const { return mPosition; }

    FxMat4f mModelMatrix = FxMat4f::Identity;

public:
    FxVec3f mPosition = FxVec3f::Zero;
    FxVec3f mRotation = FxVec3f::Zero;
    FxVec3f mScale = FxVec3f::One;


    FxRef<FxScript> mScript { nullptr };

    std::vector<FxRef<FxEntity>> Children;
};

class FxEntityManager
{
public:
    static FxEntityManager& GetGlobalManager();

    void Create(uint32 entities_per_page = 64);

    static FxRef<FxEntity> New();

private:
    FxPagedArray<FxEntity> mEntityPool;
};
