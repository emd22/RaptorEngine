#pragma once

#include <Asset/FxAssetImage.hpp>
#include <Core/FxPagedArray.hpp>
#include <Math/FxQuat.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vector.hpp>
#include <Renderer/FxCamera.hpp>

class FxEntity
{
public:
    FxEntity() { Children.reserve(4); }

    void AddChild(FxRef<FxEntity>& entity) { Children.push_back(entity); }

    virtual void MoveTo(const FxVec3f& position)
    {
        mPosition = position;
        MarkTransformOutOfDate();
    }

    virtual void MoveBy(const FxVec3f& offset)
    {
        mPosition += offset;
        MarkTransformOutOfDate();
    }

    virtual void Scale(const FxVec3f& scale);

    void RotateX(float32 rad);
    void RotateY(float32 rad);
    void RotateZ(float32 rad);

    void SetModelMatrix(const FxMat4f& other);

    FxMat4f& GetModelMatrix();

    const FxVec3f& GetPosition() const { return mPosition; }

    FX_FORCE_INLINE void MarkTransformOutOfDate()
    {
        // Note that this is handled and acted on by FxObject
        mbPhysicsTransformOutOfDate = true;
        MarkMatrixOutOfDate();
    }

    FX_FORCE_INLINE void MarkMatrixOutOfDate() { mbMatrixOutOfDate = true; }

    virtual ~FxEntity() {}

protected:
    void RecalculateModelMatrix();

public:
    FxVec3f mPosition = FxVec3f::sZero;
    FxQuat mRotation = FxQuat::sIdentity;
    FxVec3f mScale = FxVec3f::sOne;

    std::vector<FxRef<FxEntity>> Children;


protected:
    bool mbPhysicsTransformOutOfDate : 1 = false;
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
