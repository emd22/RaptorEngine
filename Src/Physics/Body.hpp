#pragma once

#include "BodyID.hpp"

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/Body.h>
#include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/PagedArray.hpp>
#include <Entity.hpp>
#include <Renderer/PrimitiveMesh.hpp>

namespace fx {
class PrimitiveMesh;

namespace physics {

struct BodyProps
{
public:
	float32 ConvexRadius = 0.001f;
	float32 Friction = 0.2f;
	float32 Restitution = 0.1f;

	/**
	 * @brief Density of the physics material in kg / m^3
	 * @default The density of white pine.
	 */
	float32 Density = 300.0f;
};


enum class eMotionType
{
	Static,
	Dynamic,
};

enum class ePrimitiveType
{
	None,
	Box,
};


class Body
{
public:
	enum eFlags
	{
		None = 0x00,
		CreateInactive = 0x01,
	};

public:
	Body() = default;

	void SetID(physics::BodyID id) { ID = id; }
	physics::BodyID GetID() const { return ID; }

	void CreatePrimitiveBody(ePrimitiveType primitive_type, const Vec3f& dimensions, physics::eMotionType motion_type,
							 const BodyProps& object_properties);

	void CreateMeshBody(const PrimitiveMesh& mesh, physics::eMotionType motion_type,
						const BodyProps& object_properties);

	void DestroyPhysicsBody();

	void Teleport(const Vec3f& position, const Quat& rotation);

	FX_FORCE_INLINE Vec3f GetPosition() { return Vec3f(mpPhysicsBody->GetPosition()); }
	FX_FORCE_INLINE Quat GetRotation() { return Quat(mpPhysicsBody->GetRotation()); }

	FX_FORCE_INLINE JPH::Body* GetBody() { return mpPhysicsBody; };
	FX_FORCE_INLINE const JPH::BodyID& GetBodyID() { return mpPhysicsBody->GetID(); };

	/// Saves the ID for the linked object.
	FX_FORCE_INLINE void SetObjectID(ObjectID id) { mObjectID = id; }
	/// Retrieves the ID for the linked object
	FX_FORCE_INLINE ObjectID GetObjectID() const { return mObjectID; }

	FX_FORCE_INLINE physics::eMotionType GetMotionType() const { return mMotionType; }

	FX_FORCE_INLINE Name& GetName() { return mColliderName; }
	FX_FORCE_INLINE void SetName(const String& name) { mColliderName.Set(name); }
	FX_FORCE_INLINE void SetName(const std::string& name) { mColliderName.Set(name); }

	~Body() = default;

private:
	void CreateJoltBody(JPH::ShapeRefC shape, eFlags flags, physics::eMotionType type, const BodyProps& properties);
	void UpdateJoltBody(JPH::ShapeRefC shape, Body::eFlags flags, physics::eMotionType motion_type,
						const BodyProps& properties);

public:
	JPH::Body* mpPhysicsBody = nullptr;
	physics::eMotionType mMotionType = physics::eMotionType::Static;

	bool mbHasPhysicsBody = false;

	Vec3f Dimensions = Vec3f::sOne;
	ePrimitiveType PrimitiveType = ePrimitiveType::None;

	physics::BodyID ID = physics::BodyID::scNull;


private:
	Name mColliderName;
	ObjectID mObjectID = ObjectID::scNull;
};

} // namespace physics
} // namespace fx
