#include "Entity.hpp"

#include <Engine.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx {

static_assert(sizeof(ObjectID) == sizeof(uint32));
static_assert(sizeof(eTransformMode) == sizeof(uint32));
static_assert(sizeof(Mat4f) == 16 * sizeof(float32));
static_assert(offsetof(RxEntityCore, matrix) == 0);
static_assert(offsetof(RxEntityCore, position) == 64);
static_assert(offsetof(RxEntityCore, rotation) == 80);
static_assert(offsetof(RxEntityCore, rotation_origin) == 96);
static_assert(offsetof(RxEntityCore, scale) == 112);
static_assert(offsetof(RxEntityCore, transform_mode) == 116);
static_assert(offsetof(RxEntityCore, id) == 120);

Entity::Entity(RxEntityCore* core, bool owns_core)
	: mpCore(core),
	  mbOwnsCore(owns_core),
	  ID(*reinterpret_cast<ObjectID*>(&mpCore->id)),
	  mPosition(*reinterpret_cast<Vec3f*>(mpCore->position)),
	  mRotation(*reinterpret_cast<Quat*>(mpCore->rotation)),
	  mScale(mpCore->scale),
	  RotationOrigin(*reinterpret_cast<Vec3f*>(mpCore->rotation_origin)),
	  TransformMode(*reinterpret_cast<eTransformMode*>(&mpCore->transform_mode)),
	  mWorldMatrix(*reinterpret_cast<Mat4f*>(mpCore->matrix))
{
}

Entity::Entity() : Entity(rx_entity_core_new(), true) {}

Entity::Entity(RxEntityCore* borrowed_core) : Entity(borrowed_core, false) {}

void Entity::RotateByAxis(const Vec3f& axis, float32 rad)
{
	float32 rotation[4];
	rx_entity_rotated_by_axis(mpCore, &axis.mData[0], rad, rotation);

	// Through SetRotation so overrides (e.g. Object syncing its physics body and attached objects) see the change
	SetRotation(Quat(rotation[0], rotation[1], rotation[2], rotation[3]));
}

void Entity::SubmitMatrixIfNeeded()
{
	// There is no object id assigned to the object yet, break
	if (ID.IsInvalid()) {
		return;
	}

	if (!rx_entity_submit_needed(mpCore, renderer::gGraphics->GetFrameNumber())) {
		return;
	}

	gObjectManager->Submit(ID.GetID(), mWorldMatrix);
}

} // namespace fx
