#pragma once

#include "FxMaterial.hpp"
#include "Renderer/Backend/RxFrameData.hpp"
#include "Renderer/Backend/RxPipeline.hpp"

#include <sys/types.h>

#include <Asset/FxAssetImage.hpp>
#include <Core/FxPagedArray.hpp>
#include <Math/FxQuat.hpp>
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

    virtual void MoveTo(const FxVec3f& position)
    {
        mPosition = position;

        MarkMatrixOutOfDate();

        // UpdateTranslation();
    }

    virtual void MoveBy(const FxVec3f& offset)
    {
        mPosition += offset;

        MarkMatrixOutOfDate();

        // UpdateTranslation();
    }


    void Scale(const FxVec3f& scale)
    {
        mScale *= scale;

        MarkMatrixOutOfDate();
        // mModelMatrix = FxMat4f::AsScale(mScale) * mModelMatrix;
    }

    void RotateX(float rad)
    {
        mRotation2 = mRotation2 * FxQuat::FromAxisAngle(FxVec3f::sRight, rad);

        MarkMatrixOutOfDate();
        // mRotation.X += rad;
        // mModelMatrix = FxMat4f::AsRotationX(mRotation.X);
        // mModelMatrix.Print();
    }

    void RotateY(float rad)
    {
        mRotation2 = mRotation2 * FxQuat::FromAxisAngle(FxVec3f::sUp, rad);

        MarkMatrixOutOfDate();

        // mRotation.Y += rad;
        // mModelMatrix = FxMat4f::AsRotationY(mRotation.Y);
    }

    void RotateZ(float rad)
    {
        mRotation2 = mRotation2 * FxQuat::FromAxisAngle(FxVec3f::sForward, rad);

        MarkMatrixOutOfDate();

        // mRotation2 = mRotation2 * FxQuat::
        // mRotation.Z += rad;
        // mModelMatrix = FxMat4f::AsRotationZ(mRotation.Z);
    }

    void SetModelMatrix(const FxMat4f& other)
    {
        // We do not want the next update to replace the new matrix
        mbMatrixOutOfDate = false;

        mModelMatrix = other;
    }


    FxMat4f& GetModelMatrix()
    {
        if (mbMatrixOutOfDate) {
            RecalculateModelMatrix();
        }

        return mModelMatrix;
    }

    const FxVec3f& GetPosition() const { return mPosition; }

    FX_FORCE_INLINE void MarkMatrixOutOfDate() { mbMatrixOutOfDate = true; }

    virtual ~FxEntity() {}


protected:
    // inline void UpdateTranslation() { mModelMatrix = FxMat4f::AsScale(mScale) * FxMat4f::AsTranslation(mPosition); }

    void RecalculateModelMatrix()
    {
        mModelMatrix = FxMat4f::AsScale(mScale) * FxMat4f::AsRotation(mRotation2) * FxMat4f::AsTranslation(mPosition);
    }


public:
    FxVec3f mPosition = FxVec3f::sZero;
    FxQuat mRotation2 = FxQuat::sIdentity;
    // FxVec3f mRotation = FxVec3f::Zero;
    FxVec3f mScale = FxVec3f::sOne;

    std::vector<FxRef<FxEntity>> Children;


private:
    bool mbMatrixOutOfDate : 1 = false;
    FxMat4f mModelMatrix = FxMat4f::Identity;
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
