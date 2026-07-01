#include "Entity.hpp"

#include <Engine.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/DeferredRenderer.hpp>

namespace fx {


EntityManager& EntityManager::GetGlobalManager()
{
    static EntityManager global_manager;
    return global_manager;
}

void EntityManager::Create(uint32 entities_per_page) { GetGlobalManager().mEntityPool.Create(entities_per_page); }

Ref<Entity> EntityManager::New()
{
    EntityManager& global_manager = GetGlobalManager();

    if (!global_manager.mEntityPool.IsInited()) {
        global_manager.Create();
    }

    return Ref<Entity>::New(global_manager.mEntityPool.Insert());
}

void Entity::ScaleBy(const float scale) { SetScale(mScale * scale); }

void Entity::SetScale(const float scale)
{
    mScale = scale;
    MarkTransformOutOfDate();
}

void Entity::RotateX(float32 rad)
{
    mRotation = mRotation * Quat::FromAxisAngle(Vec3f::sRight, rad);
    MarkTransformOutOfDate();
}


void Entity::RotateY(float32 rad)
{
    mRotation = mRotation * Quat::FromAxisAngle(Vec3f::sUp, rad);
    MarkTransformOutOfDate();
}


void Entity::RotateZ(float32 rad)
{
    mRotation = mRotation * Quat::FromAxisAngle(Vec3f::sForward, rad);
    MarkTransformOutOfDate();
}

void Entity::SetModelMatrix(const Mat4f& other)
{
    // We do not want the next update to replace the new matrix
    mbMatrixOutOfDate = false;

    mModelMatrix = other;

    // Trigger an immediate update
    SubmitMatrixIfNeeded();
}

// const Mat4f& Entity::GetNormalMatrix()
// {
//     if (mbMatrixOutOfDate) {
//         RecalculateModelMatrix();
//     }

//     // return mNormalMatrix;
// }

Mat4f& Entity::GetModelMatrix()
{
    if (mbMatrixOutOfDate) {
        RecalculateModelMatrix();
    }

    return mModelMatrix;
}

void Entity::SubmitMatrixIfNeeded()
{
    // There is no object id assigned to the object yet, break
    if (ID.IsInvalid() || mMatrixUpdateFramesRemaining <= 0) {
        return;
    }

    gObjectManager->Submit(ID.GetID(), mModelMatrix);

    --mMatrixUpdateFramesRemaining;
}

void Entity::RecalculateModelMatrix()
{
    if (TransformMode == eTransformMode::Default) {
        mModelMatrix = Mat4f::AsScale(Vec3f(mScale)) * Mat4f::AsRotation(mRotation) * Mat4f::AsTranslation(mPosition);
    }
    else if (TransformMode == eTransformMode::TransformFromOrigin) {
        mModelMatrix = Mat4f::AsScale(Vec3f(mScale)) * Mat4f::AsTranslation(RotationOrigin) *
                       Mat4f::AsRotation(mRotation) * Mat4f::AsTranslation(-RotationOrigin) *
                       Mat4f::AsTranslation(mPosition);
    }

    mMatrixUpdateFramesRemaining = renderer::FramesInFlight;

    // mNormalMatrix = mModelMatrix.Inverse().Transposed();
    mbMatrixOutOfDate = false;
}

} // namespace fx
