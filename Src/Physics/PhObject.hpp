#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/Body.h>
#include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/PagedArray.hpp>
#include <Entity.hpp>
#include <Material.hpp>
#include <Renderer/PrimitiveMesh.hpp>

namespace fx {

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


enum class PhMotionType
{
    eStatic,
    eDynamic,
};

enum class PhPrimitiveType
{
    eBox,
};


class PhObject
{
public:
    enum Flags
    {
        eNone = 0x00,
        eCreateInactive = 0x01,
    };


public:
    void CreatePrimitiveBody(PhPrimitiveType primitive_type, const Vec3f& dimensions, PhMotionType motion_type,
                             const PhProperties& object_properties);

    void CreateMeshBody(const PrimitiveMesh& mesh, PhMotionType motion_type, const PhProperties& object_properties);

    void DestroyPhysicsBody();

    void Teleport(Vec3f position, Quat rotation);

    FX_FORCE_INLINE Vec3f GetPosition() { return Vec3f(mpPhysicsBody->GetPosition()); }
    FX_FORCE_INLINE Quat GetRotation() { return Quat(mpPhysicsBody->GetRotation()); }

    FX_FORCE_INLINE JPH::Body* GetBody() { return mpPhysicsBody; };
    FX_FORCE_INLINE const JPH::BodyID& GetBodyId() { return mpPhysicsBody->GetID(); };

    FX_FORCE_INLINE PhMotionType GetMotionType() const { return mMotionType; }

private:
    void CreateJoltBody(JPH::ShapeRefC shape, Flags flags, PhMotionType type, const PhProperties& properties);

public:
    JPH::Body* mpPhysicsBody = nullptr;
    PhMotionType mMotionType = PhMotionType::eStatic;

    bool mbHasPhysicsBody : 1 = false;
};

} // namespace fx
