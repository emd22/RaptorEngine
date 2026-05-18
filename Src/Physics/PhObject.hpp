#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/Body.h>
#include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/PagedArray.hpp>
#include <Entity.hpp>
#include <Renderer/PrimitiveMesh.hpp>

namespace fx {

using PhObjectId = uint32;
constexpr PhObjectId PhObjectIdNull = UINT32_MAX;

class PrimitiveMesh;

struct PhProperties
{
public:
    PhProperties() = default;

    float32 ConvexRadius = 0.001f;
    float32 Friction = 0.2f;
    float32 Restitution = 0.1f;

    /**
     * @brief Density of the physics material in kg / m^3
     * @default The density of white pine.
     */
    float32 Density = 300.0f;
};


enum class ePhMotionType
{
    Static,
    Dynamic,
};

enum class ePhPrimitiveType
{
    None,
    Box,
};


class PhObject
{
public:
    enum eFlags
    {
        None = 0x00,
        CreateInactive = 0x01,
    };

public:
    PhObject() = default;

    void SetId(PhObjectId id) { Id = id; }
    PhObjectId GetId() const { return Id; }

    void CreatePrimitiveBody(ePhPrimitiveType primitive_type, const Vec3f& dimensions, ePhMotionType motion_type,
                             const PhProperties& object_properties);

    void CreateMeshBody(const PrimitiveMesh& mesh, ePhMotionType motion_type, const PhProperties& object_properties);

    void DestroyPhysicsBody();

    void Teleport(const Vec3f& position, const Quat& rotation);

    FX_FORCE_INLINE Vec3f GetPosition() { return Vec3f(mpPhysicsBody->GetPosition()); }
    FX_FORCE_INLINE Quat GetRotation() { return Quat(mpPhysicsBody->GetRotation()); }

    FX_FORCE_INLINE JPH::Body* GetBody() { return mpPhysicsBody; };
    FX_FORCE_INLINE const JPH::BodyID& GetBodyId() { return mpPhysicsBody->GetID(); };

    FX_FORCE_INLINE ePhMotionType GetMotionType() const { return mMotionType; }

    FX_FORCE_INLINE Name& GetName() { return mColliderName; }
    FX_FORCE_INLINE void SetName(const String& name) { mColliderName.Set(name); }
    FX_FORCE_INLINE void SetName(const std::string& name) { mColliderName.Set(name); }

    ~PhObject() = default;

private:
    void CreateJoltBody(JPH::ShapeRefC shape, eFlags flags, ePhMotionType type, const PhProperties& properties);
    void UpdateJoltBody(JPH::ShapeRefC shape, PhObject::eFlags flags, ePhMotionType motion_type,
                        const PhProperties& properties);

public:
    JPH::Body* mpPhysicsBody = nullptr;
    ePhMotionType mMotionType = ePhMotionType::Static;

    bool mbHasPhysicsBody : 1 = false;

    Vec3f Dimensions = Vec3f::sOne;
    ePhPrimitiveType PrimitiveType = ePhPrimitiveType::None;

    PhObjectId Id = PhObjectIdNull;

private:
    Name mColliderName;
};

} // namespace fx
