#include "FxEntity.hpp"

#include <Renderer/RxDeferred.hpp>

FxEntityManager& FxEntityManager::GetGlobalManager()
{
    static FxEntityManager global_manager;
    return global_manager;
}

void FxEntityManager::Create(uint32 entities_per_page) { GetGlobalManager().mEntityPool.Create(entities_per_page); }

FxRef<FxEntity> FxEntityManager::New()
{
    FxEntityManager& global_manager = GetGlobalManager();

    if (!global_manager.mEntityPool.IsInited()) {
        global_manager.Create();
    }

    return FxRef<FxEntity>::New(global_manager.mEntityPool.Insert());
}

void FxEntity::Scale(const FxVec3f& scale)
{
    mScale *= scale;
    MarkTransformOutOfDate();
}

void FxEntity::RotateX(float32 rad)
{
    mRotation = mRotation * FxQuat::FromAxisAngle(FxVec3f::sRight, rad);
    MarkTransformOutOfDate();
}


void FxEntity::RotateY(float32 rad)
{
    mRotation = mRotation * FxQuat::FromAxisAngle(FxVec3f::sUp, rad);
    MarkTransformOutOfDate();
}


void FxEntity::RotateZ(float32 rad)
{
    mRotation = mRotation * FxQuat::FromAxisAngle(FxVec3f::sForward, rad);
    MarkTransformOutOfDate();
}

void FxEntity::SetModelMatrix(const FxMat4f& other)
{
    // We do not want the next update to replace the new matrix
    mbMatrixOutOfDate = false;

    mModelMatrix = other;
}

const FxMat4f& FxEntity::GetNormalMatrix()
{
    if (mbMatrixOutOfDate) {
        RecalculateModelMatrix();
    }

    return mNormalMatrix;
}

FxMat4f& FxEntity::GetModelMatrix()
{
    if (mbMatrixOutOfDate) {
        RecalculateModelMatrix();
    }

    return mModelMatrix;
}

void FxEntity::RecalculateModelMatrix()
{
    if (TransformMode == FxTransformMode::eDefault) {
        mModelMatrix = FxMat4f::AsScale(mScale) * FxMat4f::AsRotation(mRotation) * FxMat4f::AsTranslation(mPosition);
    }
    else if (TransformMode == FxTransformMode::eTransformFromOrigin) {
        mModelMatrix = FxMat4f::AsScale(mScale) * FxMat4f::AsTranslation(RotationOrigin) *
                       FxMat4f::AsRotation(mRotation) * FxMat4f::AsTranslation(-RotationOrigin) *
                       FxMat4f::AsTranslation(mPosition);
    }

    mNormalMatrix = mModelMatrix.Inverse().Transposed();
    mbMatrixOutOfDate = false;
}
