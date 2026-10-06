#pragma once

#include <Core/Name.hpp>
#include <Core/Ref.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Constants.hpp>
#include <raptor_ffi.h>

#define FX_VALIDATE_ENTITY_TYPE(TType_) static_assert(C_IsEntity<TType_>)

namespace fx {


class World;

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


/**
 * @brief Something with a place in the world. Where it is, which way it faces and the matrix that follows from that are
 * a record that Rust owns, and the fields here are references into it.
 */
class Entity
{
public:
	Entity();

	/// An entity whose record belongs to something else, which must outlive it
	explicit Entity(RxEntityCore* borrowed_core);

private:
	Entity(RxEntityCore* core, bool owns_core);

public:

	Entity(const Entity&) = delete;
	Entity& operator=(const Entity&) = delete;

	virtual void SetPosition(const Vec3f& position) { rx_entity_set_position(mpCore, &position.mData[0]); }

	virtual void SetRotation(const Quat& rotation) { rx_entity_set_rotation(mpCore, &rotation.mData[0]); }

	virtual void SetScale(const float scale) { rx_entity_set_scale(mpCore, scale); }

	virtual void MoveBy(const Vec3f& offset) { SetPosition(mPosition + offset); }
	void ScaleBy(const float scale) { SetScale(mScale * scale); }

	virtual void OnAttached(World* scene) {}

	/**
	 * @brief Rotate around the entity's own (local) X, Y or Z axis. A positive angle follows the right-hand rule, which
	 * in this left-handed (+Z forward) space means RotateX(+) pitches forward down and RotateY(+) yaws forward right.
	 */
	void RotateX(float32 rad) { RotateByAxis(Vec3f::sRight, rad); }
	void RotateY(float32 rad) { RotateByAxis(Vec3f::sUp, rad); }
	void RotateZ(float32 rad) { RotateByAxis(Vec3f::sForward, rad); }

	void SetModelMatrix(const Mat4f& other) { rx_entity_set_model_matrix(mpCore, &other.Rows[0].mData[0]); }

	Mat4f& GetWorldMatrix()
	{
		rx_entity_update_matrix(mpCore);
		return mWorldMatrix;
	}

	const Vec3f& GetPosition() const { return mPosition; }

	FX_FORCE_INLINE void MarkTransformOutOfDate() { rx_entity_mark_transform_out_of_date(mpCore); }

	FX_FORCE_INLINE void SetRotationOrigin(const Vec3f& origin) { rx_entity_set_rotation_origin(mpCore, &origin.mData[0]); }

	void UpdateIfOutOfDate()
	{
		rx_entity_update_matrix(mpCore);

		SubmitMatrixIfNeeded();
	}

	FX_FORCE_INLINE void MarkMatrixOutOfDate() { rx_entity_mark_matrix_out_of_date(mpCore); }

	const RxEntityCore* GetCore() const { return mpCore; }

	virtual ~Entity()
	{
		if (mbOwnsCore) {
			rx_entity_core_free(mpCore);
		}
	}

protected:
	/**
	 * @brief Submit the matrix to the object gpu buffer if there have been changes, or if it has not been synched with
	 * the current frame yet.
	 */
	void SubmitMatrixIfNeeded();

	bool IsPhysicsTransformOutOfDate() const { return rx_entity_is_physics_out_of_date(mpCore) != 0; }
	void SetPhysicsTransformOutOfDate(bool value) { rx_entity_set_physics_out_of_date(mpCore, value); }

	bool IsMatrixOutOfDate() const { return rx_entity_is_matrix_out_of_date(mpCore) != 0; }

private:
	void RotateByAxis(const Vec3f& axis, float32 rad);

private:
	RxEntityCore* mpCore = nullptr;
	bool mbOwnsCore = true;

public:
	ObjectID& ID;

	Name Name;

	Vec3f& mPosition;
	Quat& mRotation;
	float32& mScale;

	Vec3f& RotationOrigin;

	eTransformMode& TransformMode;

protected:
	Mat4f& mWorldMatrix;
};


template <typename TEntityType>
concept C_IsEntity = requires() {
	// Ensure that there is a const(expr) scEntity type parameter
	requires std::is_same_v<const eEntityType, decltype(TEntityType::scEntityType)>;

	// TEntity type is derived from Entity
	requires std::is_base_of_v<Entity, TEntityType>;
};

} // namespace fx
