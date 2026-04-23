#pragma once

#include <Asset/AxImage.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Constants.hpp>

#define FX_VALIDATE_ENTITY_TYPE(TType_) static_assert(C_IsEntity<TType_>);

namespace fx {


class Scene;

enum class eEntityType
{
    Unknown,
    Object,
    Light
};

enum class eTransformMode
{
    Default,
    TransformFromOrigin
};


class Entity
{
public:
    FX_FORCE_INLINE void SetPosition(const Vec3f& position)
    {
        mPosition = position;
        MarkTransformOutOfDate();
    }

    FX_FORCE_INLINE void SetRotation(const Quat& rotation)
    {
        mRotation = rotation;
        MarkTransformOutOfDate();
    }

    void SetScale(const float scale);

    void MoveBy(const Vec3f& offset) { SetPosition(mPosition + offset); }
    void ScaleBy(const float scale);

    virtual void OnAttached(Scene* scene) {}

    void RotateX(float32 rad);
    void RotateY(float32 rad);
    void RotateZ(float32 rad);

    void SetModelMatrix(const Mat4f& other);

    Mat4f& GetModelMatrix();
    // const Mat4f& GetNormalMatrix();

    const Vec3f& GetPosition() const { return mPosition; }

    FX_FORCE_INLINE void MarkTransformOutOfDate()
    {
        // Note that this is handled and acted on by Object
        mbPhysicsTransformOutOfDate = true;
        MarkMatrixOutOfDate();
    }

    FX_FORCE_INLINE void SetRotationOrigin(const Vec3f& origin)
    {
        RotationOrigin = origin;
        TransformMode = eTransformMode::TransformFromOrigin;
        MarkMatrixOutOfDate();
    }

    void UpdateIfOutOfDate()
    {
        if (mbMatrixOutOfDate) {
            RecalculateModelMatrix();
        }

        SubmitMatrixIfNeeded();
    }

    FX_FORCE_INLINE void MarkMatrixOutOfDate() { mbMatrixOutOfDate = true; }

    virtual ~Entity();

protected:
    void RecalculateModelMatrix();

    /**
     * @brief Submit the matrix to the object gpu buffer if there have been changes, or if it has not been synched with
     * the current frame yet.
     */
    void SubmitMatrixIfNeeded();

public:
    uint32 ObjectId = UINT32_MAX;

    Name Name;

    Vec3f mPosition = Vec3f::sZero;
    Quat mRotation = Quat::sIdentity;
    float32 mScale = 1.0f;

    Vec3f RotationOrigin = Vec3f::sZero;

    eTransformMode TransformMode = eTransformMode::Default;

protected:
    bool mbPhysicsTransformOutOfDate : 1 = true;
    bool mbMatrixOutOfDate : 1 = true;

    Mat4f mModelMatrix = Mat4f::sIdentity;

    uint32 mMatrixUpdateFramesRemaining = renderer::FramesInFlight;
    // Mat4f mNormalMatrix = Mat4f::Identity;
};

class EntityManager
{
public:
    static EntityManager& GetGlobalManager();

    void Create(uint32 entities_per_page = 64);

    static Ref<Entity> New();

private:
    PagedArray<Entity> mEntityPool;
};


template <typename TEntityType>
concept C_IsEntity = requires() {
    // Ensure that there is a const(expr) scEntity type parameter
    requires std::is_same_v<const eEntityType, decltype(TEntityType::scEntityType)>;

    // TEntity type is derived from Entity
    requires std::is_base_of_v<Entity, TEntityType>;
};

} // namespace fx
